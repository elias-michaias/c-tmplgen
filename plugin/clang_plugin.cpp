#include "tmplgen_plugin_support.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

#include <mutex>
#include <string>
#include <vector>

namespace {

std::once_flag RunOnce;
int RunResult = 0;

class TmplgenPlugin final : public clang::PluginASTAction {
public:
    bool ParseArgs(const clang::CompilerInstance &compiler,
                   const std::vector<std::string> &arguments) override
    {
        std::call_once(RunOnce, [&] {
            RunResult = tmplgen_plugin_support::runTmplgen(
                compiler, arguments);
        });
        return RunResult == 0;
    }

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &, llvm::StringRef) override
    {
        return std::make_unique<clang::ASTConsumer>();
    }

    ActionType getActionType() override { return AddBeforeMainAction; }
};

clang::FrontendPluginRegistry::Add<TmplgenPlugin> Registration(
    "tmplgen", "generate template specializations before parsing the TU");

} // namespace
