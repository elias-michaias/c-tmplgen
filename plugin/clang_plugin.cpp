#include "tmplgen_plugin_support.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

#include <string>
#include <vector>

namespace {

class TmplgenPlugin final : public clang::PluginASTAction {
public:
    bool ParseArgs(const clang::CompilerInstance &compiler,
                   const std::vector<std::string> &arguments) override
    {
        // ParseArgs runs once per translation unit, after Clang has loaded the
        // main-file buffer and before it parses the AST. Keep generation state
        // per plugin instance so multiple inputs in one process are not skipped.
        if (HasRun) return RunResult == 0;
        HasRun = true;
        RunResult = tmplgen_plugin_support::runTmplgen(compiler, arguments);
        return RunResult == 0;
    }

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<clang::ASTConsumer>();
    }

    ActionType getActionType() override { return AddBeforeMainAction; }

private:
    bool HasRun = false;
    int RunResult = 0;
};

clang::FrontendPluginRegistry::Add<TmplgenPlugin> Registration(
    "tmplgen", "generate template specializations before parsing the TU");

} // namespace
