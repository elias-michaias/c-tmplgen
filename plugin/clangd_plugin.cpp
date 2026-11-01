#include "tmplgen_plugin_support.hpp"

#include "FeatureModule.h"
#include "LSPBinder.h"
#include "XRefs.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "Diagnostics.h"
#include "TUScheduler.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"

#include <cctype>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace clang::clangd {
namespace {

std::mutex GeneratorMutex;

std::optional<std::string> decodeLineDirectivePath(llvm::StringRef Directive)
{
    size_t Quote = Directive.find('"');
    if (Quote == llvm::StringRef::npos) return std::nullopt;
    std::string Path;
    for (size_t I = Quote + 1U; I < Directive.size(); ++I) {
        char C = Directive[I];
        if (C == '"') return Path;
        if (C == '\\' && I + 1U < Directive.size()) {
            C = Directive[++I];
            if (C == 'n') C = '\n';
            else if (C == 't') C = '\t';
            else if (C == 'r') C = '\r';
        }
        Path.push_back(C);
    }
    return std::nullopt;
}

std::optional<Location> templateFunctionLocation(
    const LocatedSymbol &Symbol, const Location &GeneratedLocation,
    llvm::StringRef MainPath)
{
    auto GeneratedBuffer = llvm::MemoryBuffer::getFile(
        GeneratedLocation.uri.file());
    if (!GeneratedBuffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> Lines;
    (*GeneratedBuffer)->getBuffer().split(Lines, '\n');
    size_t TargetLine = GeneratedLocation.range.start.line;
    if (TargetLine >= Lines.size()) return std::nullopt;

    llvm::StringRef MacroLine = Lines[TargetLine].trim();
    if (!MacroLine.consume_front("#define")) return std::nullopt;
    MacroLine = MacroLine.ltrim();
    llvm::StringRef MacroName = MacroLine.take_while([](char C) {
        return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
    });
    if (MacroName != Symbol.Name) return std::nullopt;

    size_t TemplateLine = 0U;
    std::optional<std::string> TemplatePath;
    size_t LowerBound = TargetLine > 16U ? TargetLine - 16U : 0U;
    for (size_t I = TargetLine; I > LowerBound; --I) {
        llvm::StringRef Candidate = Lines[I - 1U].trim();
        if (!Candidate.consume_front("#line ")) continue;
        Candidate = Candidate.ltrim();
        size_t Digits = Candidate.find_first_not_of("0123456789");
        if (Digits == llvm::StringRef::npos || Digits == 0U) continue;
        unsigned ParsedLine = 0U;
        if (Candidate.take_front(Digits).getAsInteger(10, ParsedLine) ||
            ParsedLine == 0U) {
            continue;
        }
        TemplatePath = decodeLineDirectivePath(Candidate.drop_front(Digits));
        if (TemplatePath) {
            TemplateLine = ParsedLine;
            break;
        }
    }
    if (!TemplatePath || TemplateLine == 0U) return std::nullopt;

    auto TemplateBuffer = llvm::MemoryBuffer::getFile(*TemplatePath);
    if (!TemplateBuffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> TemplateLines;
    (*TemplateBuffer)->getBuffer().split(TemplateLines, '\n');
    if (TemplateLine > TemplateLines.size()) return std::nullopt;
    llvm::StringRef Head = TemplateLines[TemplateLine - 1U];
    size_t NameStart = Head.find(Symbol.Name);
    if (NameStart == llvm::StringRef::npos) return std::nullopt;
    size_t NameEnd = NameStart + Symbol.Name.size();
    if ((NameStart > 0U &&
         (std::isalnum(static_cast<unsigned char>(Head[NameStart - 1U])) ||
          Head[NameStart - 1U] == '_')) ||
        (NameEnd < Head.size() &&
         (std::isalnum(static_cast<unsigned char>(Head[NameEnd])) ||
          Head[NameEnd] == '_'))) {
        return std::nullopt;
    }
    return Location{
        URIForFile::canonicalize(*TemplatePath, MainPath),
        Range{{static_cast<int>(TemplateLine - 1U),
               static_cast<int>(NameStart)},
              {static_cast<int>(TemplateLine - 1U),
               static_cast<int>(NameEnd)}}};
}

bool isToggleTarget(const TextDocumentPositionParams &Params,
                    const LocatedSymbol &Symbol,
                    Location &Toggle)
{
    if (!Symbol.Definition || *Symbol.Definition == Symbol.PreferredDeclaration)
        return false;
    if (Symbol.Definition->uri.file() == Params.textDocument.uri.file() &&
        Symbol.Definition->range.contains(Params.position)) {
        Toggle = Symbol.PreferredDeclaration;
        return true;
    }
    if (Symbol.PreferredDeclaration.uri.file() == Params.textDocument.uri.file() &&
        Symbol.PreferredDeclaration.range.contains(Params.position)) {
        Toggle = *Symbol.Definition;
        return true;
    }
    return false;
}

class TmplgenASTListener final : public FeatureModule::ASTListener {
public:
    void beforeExecute(clang::CompilerInstance &compiler) override
    {
        std::lock_guard<std::mutex> lock(GeneratorMutex);
        int result = tmplgen_plugin_support::runTmplgen(
            compiler, {}, true);
        if (result != 0) {
            unsigned diagnostic = compiler.getDiagnostics().getCustomDiagID(
                clang::DiagnosticsEngine::Error,
                "tmplgen could not update generated files; see the clangd log");
            compiler.getDiagnostics().Report(diagnostic);
        }
    }

    void sawDiagnostic(const clang::Diagnostic &diagnostic,
                       clangd::Diag &diag) override
    {
        const clang::SourceManager &sourceManager =
            diagnostic.getSourceManager();
        clang::PresumedLoc presumed = sourceManager.getPresumedLoc(
            diagnostic.getLocation());
        if (!presumed.isValid()) return;
        clang::FileID mainFile = sourceManager.getMainFileID();
        if (mainFile.isInvalid() ||
            sourceManager.getFileID(diagnostic.getLocation()) == mainFile) {
            return;
        }
        llvm::StringRef mainPath = sourceManager.getFilename(
            sourceManager.getLocForStartOfFile(mainFile));
        if (mainPath != presumed.getFilename() || presumed.getLine() == 0U) {
            return;
        }
        diag.File = presumed.getFilename();
        diag.AbsFile = diag.File;
        diag.InsideMainFile = true;
        int line = static_cast<int>(presumed.getLine() - 1U);
        diag.Range = {{line, 0}, {line, 0}};
    }
};

class TmplgenFeatureModule final : public FeatureModule {
public:
    void initializeLSP(LSPBinder &Bind, const llvm::json::Object &,
                       llvm::json::Object &) override
    {
        Bind.method("textDocument/definition", this,
                    &TmplgenFeatureModule::onDefinition);
    }

    std::unique_ptr<ASTListener> astListeners() override
    {
        return std::make_unique<TmplgenASTListener>();
    }

private:
    void onDefinition(const TextDocumentPositionParams &Params,
                      Callback<std::vector<Location>> Reply)
    {
        scheduler().runWithAST(
            "tmplgen definition", Params.textDocument.uri.file(),
            [this, Params, Reply = std::move(Reply)](
                llvm::Expected<InputsAndAST> In) mutable {
                if (!In) return Reply(In.takeError());
                std::vector<LocatedSymbol> Symbols =
                    locateSymbolAt(In->AST, Params.position, index());
                std::vector<Location> Definitions;
                for (const LocatedSymbol &Symbol : Symbols) {
                    Location Toggle;
                    if (isToggleTarget(Params, Symbol, Toggle)) {
                        return Reply(std::vector<Location>{std::move(Toggle)});
                    }
                    Location Definition =
                        Symbol.Definition.value_or(Symbol.PreferredDeclaration);
                    if (std::optional<Location> Template =
                            templateFunctionLocation(
                                Symbol, Definition,
                                Params.textDocument.uri.file())) {
                        Definition = std::move(*Template);
                    }
                    Definitions.push_back(std::move(Definition));
                }
                Reply(std::move(Definitions));
            });
    }
};

FeatureModuleRegistry::Add<TmplgenFeatureModule> Registration(
    "tmplgen", "regenerate C template specializations while parsing files");

} // namespace
} // namespace clang::clangd
