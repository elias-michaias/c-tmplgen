#include "tmplgen_plugin_support.hpp"

#include "FeatureModule.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "Diagnostics.h"

#include <memory>
#include <mutex>
#include <vector>

namespace clang::clangd {
namespace {

std::mutex GeneratorMutex;

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
    std::unique_ptr<ASTListener> astListeners() override
    {
        return std::make_unique<TmplgenASTListener>();
    }
};

FeatureModuleRegistry::Add<TmplgenFeatureModule> Registration(
    "tmplgen", "regenerate C template specializations while parsing files");

} // namespace
} // namespace clang::clangd
