#include "tmplgen_plugin_support.hpp"

#include "FeatureModule.h"
#include "LSPBinder.h"
#include "SourceCode.h"
#include "XRefs.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
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

bool isIdentifierChar(char C)
{
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
}

std::optional<size_t> identifierPosition(llvm::StringRef Text,
                                         llvm::StringRef Name)
{
    size_t Position = 0U;
    while ((Position = Text.find(Name, Position)) != llvm::StringRef::npos) {
        size_t End = Position + Name.size();
        if ((Position == 0U || !isIdentifierChar(Text[Position - 1U])) &&
            (End == Text.size() || !isIdentifierChar(Text[End]))) {
            return Position;
        }
        Position = End;
    }
    return std::nullopt;
}

int braceDepthAt(llvm::StringRef Text, size_t Position)
{
    int Depth = 0;
    for (size_t I = 0U; I < Position; ++I) {
        if (Text[I] == '{') ++Depth;
        else if (Text[I] == '}' && Depth > 0) --Depth;
    }
    return Depth;
}

bool namesTypeDeclaration(llvm::StringRef Code, llvm::StringRef Name,
                          size_t &Column)
{
    std::optional<size_t> Position = identifierPosition(Code, Name);
    if (!Position || braceDepthAt(Code, *Position) != 0) return false;
    size_t End = *Position + Name.size();
    bool HasSemicolon = Code.find(';', End) != llvm::StringRef::npos;
    bool HasClosingBrace = Code.rfind('}', *Position) != llvm::StringRef::npos;
    bool HasTagKeyword = false;
    for (llvm::StringRef Keyword : {"struct", "union"}) {
        size_t Found = Code.take_front(*Position).rfind(Keyword);
        if (Found == llvm::StringRef::npos) continue;
        size_t KeywordEnd = Found + Keyword.size();
        if (KeywordEnd <= *Position &&
            Code.slice(KeywordEnd, *Position).trim().empty()) {
            HasTagKeyword = true;
            break;
        }
    }
    if (!HasSemicolon && !HasClosingBrace && !HasTagKeyword) return false;
    Column = *Position;
    return true;
}

std::optional<Location> fallbackTemplateTypeLocation(
    const LocatedSymbol &Symbol, const Location &Definition,
    llvm::StringRef MainPath)
{
    llvm::StringRef Path = Definition.uri.file();
    if (!Path.ends_with(".tmpl.h") && !Path.ends_with(".tmpl.c")) {
        return std::nullopt;
    }
    auto Buffer = llvm::MemoryBuffer::getFile(Path);
    if (!Buffer) return std::nullopt;
    llvm::SmallVector<llvm::StringRef, 256> Lines;
    (*Buffer)->getBuffer().split(Lines, '\n');
    size_t TargetLine = Definition.range.start.line;
    if (TargetLine >= Lines.size()) return std::nullopt;

    llvm::StringRef MacroLine = Lines[TargetLine].trim();
    if (!MacroLine.consume_front("#define")) return std::nullopt;
    MacroLine = MacroLine.ltrim();
    llvm::StringRef MacroName = MacroLine.take_while(isIdentifierChar);
    if (MacroName != Symbol.Name) return std::nullopt;

    bool PendingTemplate = false;
    bool InTypeDeclaration = false;
    int Depth = 0;
    for (size_t I = TargetLine + 1U; I < Lines.size(); ++I) {
        llvm::StringRef Trimmed = Lines[I].trim();
        if (Trimmed.starts_with("// @template")) {
            PendingTemplate = true;
            InTypeDeclaration = false;
            Depth = 0;
            continue;
        }
        if (!PendingTemplate || Trimmed.empty() ||
            Trimmed.starts_with("//") || Trimmed.starts_with("#")) {
            continue;
        }
        llvm::StringRef Code = Trimmed;
        size_t Comment = Code.find("//");
        if (Comment != llvm::StringRef::npos) Code = Code.take_front(Comment);
        if (!InTypeDeclaration) {
            if (!Code.starts_with("typedef") && !Code.starts_with("struct") &&
                !Code.starts_with("union")) {
                PendingTemplate = false;
                continue;
            }
            InTypeDeclaration = true;
            Depth = 0;
        }
        size_t Column = 0U;
        if (namesTypeDeclaration(Code, Symbol.Name, Column)) {
            return Location{
                URIForFile::canonicalize(Path, MainPath),
                Range{{static_cast<int>(I), static_cast<int>(Column)},
                      {static_cast<int>(I),
                       static_cast<int>(Column + Symbol.Name.size())}}};
        }
        for (char C : Code) {
            if (C == '{') ++Depth;
            else if (C == '}' && Depth > 0) --Depth;
        }
        if (Depth == 0 && Code.contains(';')) {
            PendingTemplate = false;
            InTypeDeclaration = false;
        }
    }
    return std::nullopt;
}

std::optional<std::pair<llvm::StringRef, size_t>> constraintWordAt(
    llvm::StringRef Line, size_t Cursor)
{
    size_t Comment = Line.find("//");
    if (Comment == llvm::StringRef::npos) return std::nullopt;
    llvm::StringRef Annotation = Line.drop_front(Comment + 2U);
    if (!Annotation.contains("@template") && !Annotation.contains("@where")) {
        return std::nullopt;
    }

    struct Operator {
        size_t Start;
        size_t End;
    };
    llvm::SmallVector<Operator, 4> Operators;
    for (size_t I = Comment + 2U; I + 1U < Line.size(); ++I) {
        llvm::StringRef Candidate = Line.substr(I, 2U);
        if (Candidate == "<:" || Candidate == "|=") {
            Operators.push_back({I, I + 2U});
            ++I;
        }
    }

    const Operator *Current = nullptr;
    for (const Operator &Candidate : Operators) {
        if (Candidate.Start <= Cursor) Current = &Candidate;
    }
    if (Current == nullptr) return std::nullopt;

    size_t ExpressionEnd = Line.size();
    int Depth = 0;
    for (size_t I = Current->End; I < Line.size(); ++I) {
        char Character = Line[I];
        if (Character == '(' || Character == '[' || Character == '{') {
            ++Depth;
        } else if (Character == ')' || Character == ']' || Character == '}') {
            if (Depth > 0) --Depth;
        } else if (Character == ',' && Depth == 0) {
            ExpressionEnd = I;
            break;
        }
    }
    if (Cursor > ExpressionEnd) return std::nullopt;

    size_t TokenStart = Cursor;
    while (TokenStart > 0U && isIdentifierChar(Line[TokenStart - 1U])) {
        --TokenStart;
    }
    size_t TokenEnd = Cursor;
    while (TokenEnd < Line.size() && isIdentifierChar(Line[TokenEnd])) {
        ++TokenEnd;
    }
    bool OnOperator = Cursor >= Current->Start && Cursor <= Current->End;
    if (!OnOperator && TokenStart < TokenEnd &&
        TokenStart >= Current->End && TokenStart < ExpressionEnd) {
        return std::pair<llvm::StringRef, size_t>{
            Line.slice(TokenStart, TokenEnd), TokenStart};
    }

    size_t Search = std::max(Cursor, Current->End);
    while (Search < ExpressionEnd && !isIdentifierChar(Line[Search])) ++Search;
    if (Search == ExpressionEnd) return std::nullopt;
    size_t Start = Search;
    while (Start < ExpressionEnd && isIdentifierChar(Line[Start])) ++Start;
    return std::pair<llvm::StringRef, size_t>{
        Line.slice(Search, Start), Search};
}

std::optional<Location> sourceLocationToDefinition(
    const SourceManager &SourceMgr, const LangOptions &LangOpts,
    SourceLocation Loc, llvm::StringRef MainPath)
{
    if (Loc.isInvalid()) return std::nullopt;
    Loc = SourceMgr.getSpellingLoc(Loc);
    if (Loc.isInvalid()) return std::nullopt;
    FileID FID = SourceMgr.getFileID(Loc);
    if (FID.isInvalid()) return std::nullopt;
    auto File = SourceMgr.getFileEntryRefForID(FID);
    if (!File) return std::nullopt;
    std::optional<std::string> Path =
        getCanonicalPath(*File, SourceMgr.getFileManager());
    if (!Path) return std::nullopt;
    unsigned Length = Lexer::MeasureTokenLength(Loc, SourceMgr, LangOpts);
    if (Length == 0U) return std::nullopt;
    return Location{
        URIForFile::canonicalize(*Path, MainPath),
        halfOpenToRange(
            SourceMgr,
            CharSourceRange::getCharRange(
                Loc, Loc.getLocWithOffset(Length)))};
}

std::optional<Location> macroDefinitionLocation(
    ParsedAST &AST, llvm::StringRef Name, SourceLocation UseLoc,
    llvm::StringRef MainPath)
{
    Preprocessor &PP = AST.getPreprocessor();
    IdentifierInfo *Identifier = PP.getIdentifierInfo(Name);
    if (Identifier == nullptr || !Identifier->hadMacroDefinition()) {
        return std::nullopt;
    }

    MacroDefinition Definition;
    if (UseLoc.isValid()) {
        Definition = PP.getMacroDefinitionAtLoc(Identifier, UseLoc);
    }
    const MacroInfo *Info = Definition.getMacroInfo();
    if (Info == nullptr) {
        Definition = PP.getMacroDefinition(Identifier);
        Info = Definition.getMacroInfo();
    }
    if (Info == nullptr) return std::nullopt;
    return sourceLocationToDefinition(
        AST.getSourceManager(), AST.getLangOpts(),
        Info->getDefinitionLoc(), MainPath);
}

const NamedDecl *findTypeDeclaration(const DeclContext &Context,
                                     llvm::StringRef Name)
{
    const NamedDecl *Fallback = nullptr;
    for (const Decl *Declaration : Context.decls()) {
        const auto *Named = dyn_cast<NamedDecl>(Declaration);
        if (Named == nullptr || Named->getIdentifier() == nullptr ||
            Named->getName() != Name ||
            (!isa<TagDecl>(Named) && !isa<TypedefNameDecl>(Named))) {
            continue;
        }
        if (const auto *Tag = dyn_cast<TagDecl>(Named);
            Tag != nullptr && Tag->isThisDeclarationADefinition()) {
            return Named;
        }
        if (Fallback == nullptr) Fallback = Named;
    }
    for (const Decl *Declaration : Context.decls()) {
        const auto *Inner = dyn_cast<DeclContext>(Declaration);
        if (Inner == nullptr) continue;
        if (const NamedDecl *Found = findTypeDeclaration(*Inner, Name)) {
            return Found;
        }
    }
    return Fallback;
}

std::vector<Location> constraintDefinitionLocations(
    ParsedAST &AST, const TextDocumentPositionParams &Params,
    llvm::StringRef MainPath)
{
    const SourceManager &SourceMgr = AST.getSourceManager();
    FileID MainFile = SourceMgr.getMainFileID();
    if (MainFile.isInvalid()) return {};
    llvm::StringRef Code =
        SourceMgr.getBufferOrFake(MainFile).getBuffer();
    llvm::Expected<size_t> Offset =
        positionToOffset(Code, Params.position, false);
    if (!Offset) {
        llvm::consumeError(Offset.takeError());
        return {};
    }

    size_t Cursor = *Offset;
    size_t LineStart = 0U;
    if (Cursor > 0U) {
        size_t Newline = Code.rfind('\n', Cursor - 1U);
        if (Newline != llvm::StringRef::npos) LineStart = Newline + 1U;
    }
    size_t LineEnd = Code.find('\n', Cursor);
    if (LineEnd == llvm::StringRef::npos) LineEnd = Code.size();
    llvm::StringRef Line = Code.slice(LineStart, LineEnd);
    std::optional<std::pair<llvm::StringRef, size_t>> Word =
        constraintWordAt(Line, Cursor - LineStart);
    if (!Word) return {};

    SourceLocation UseLoc = SourceMgr
        .getLocForStartOfFile(MainFile)
        .getLocWithOffset(LineStart + Word->second);
    if (std::optional<Location> Macro = macroDefinitionLocation(
            AST, Word->first, UseLoc, MainPath)) {
        return {std::move(*Macro)};
    }
    const NamedDecl *Type = findTypeDeclaration(
        *AST.getASTContext().getTranslationUnitDecl(), Word->first);
    if (Type == nullptr) return {};
    if (std::optional<Location> Declaration = sourceLocationToDefinition(
            SourceMgr, AST.getLangOpts(), Type->getLocation(), MainPath)) {
        return {std::move(*Declaration)};
    }
    return {};
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
                std::vector<Location> ConstraintDefinitions =
                    constraintDefinitionLocations(
                        In->AST, Params, Params.textDocument.uri.file());
                if (!ConstraintDefinitions.empty()) {
                    return Reply(std::move(ConstraintDefinitions));
                }
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
                    } else if (std::optional<Location> Type =
                                   fallbackTemplateTypeLocation(
                                       Symbol, Definition,
                                       Params.textDocument.uri.file())) {
                        Definition = std::move(*Type);
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
