#ifndef TMPLGEN_PLUGIN_SUPPORT_HPP
#define TMPLGEN_PLUGIN_SUPPORT_HPP

#include "tmplgen_plugin_api.h"

#include "clang/Basic/LangOptions.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace tmplgen_plugin_support {

inline std::string absolutePath(llvm::StringRef path,
                                llvm::StringRef workingDirectory)
{
    llvm::SmallString<256> result(path);
    if (llvm::sys::path::is_relative(result)) {
        llvm::SmallString<256> base(workingDirectory);
        if (base.empty() && llvm::sys::fs::current_path(base)) base = ".";
        llvm::sys::path::append(base, result);
        result = base;
    }
    llvm::sys::path::remove_dots(result, true);
    return result.str().str();
}

inline std::string compilerWorkingDirectory(
    const clang::CompilerInstance &compiler)
{
    llvm::SmallString<256> result(compiler.getFileSystemOpts().WorkingDir);
    if (result.empty() && compiler.hasSourceManager()) {
        // clangd roots its VFS at the compile command's directory. Prefer that
        // over the server process directory when resolving relative flags.
        if (auto current = compiler.getSourceManager()
                               .getFileManager()
                               .getVirtualFileSystem()
                               .getCurrentWorkingDirectory()) {
            result = *current;
        }
    }
    if (result.empty()) {
        if (llvm::sys::fs::current_path(result)) result = ".";
    } else if (llvm::sys::path::is_relative(result)) {
        llvm::SmallString<256> current;
        if (!llvm::sys::fs::current_path(current)) {
            llvm::sys::path::append(current, result);
            result = current;
        }
    }
    llvm::sys::path::remove_dots(result, true);
    return result.str().str();
}

inline void appendEnvironmentOptions(std::vector<std::string> &options)
{
    struct EnvironmentOption {
        const char *variable;
        const char *option;
    };
    static const EnvironmentOption Paths[] = {
        {"TMPLGEN_SOURCE_ROOT", "source-root"},
        {"TMPLGEN_TEMPLATE_DIR", "template-dir"},
        {"TMPLGEN_OUTPUT", "output"},
        {"TMPLGEN_DISPATCH", "dispatch"},
        {"TMPLGEN_LINE_PATH_BASE", "line-path-base"},
        {"TMPLGEN_REFLECT", "reflect"},
    };
    size_t i;
    for (i = 0U; i < sizeof(Paths) / sizeof(Paths[0]); ++i) {
        const char *value = std::getenv(Paths[i].variable);
        if (value != nullptr && value[0] != '\0') {
            options.emplace_back(std::string(Paths[i].option) + "=" + value);
        }
    }
    if (const char *value = std::getenv("TMPLGEN_CLANG_ARGS")) {
        llvm::StringRef remaining(value);
        while (!remaining.empty()) {
            size_t end = remaining.find('\n');
            llvm::StringRef argument = end == llvm::StringRef::npos
                ? remaining : remaining.take_front(end);
            if (!argument.empty()) options.emplace_back("clang-arg=" + argument.str());
            if (end == llvm::StringRef::npos) break;
            remaining = remaining.drop_front(end + 1U);
        }
    }
    if (const char *value = std::getenv("TMPLGEN_VERBOSE")) {
        if (value[0] != '\0' && std::string(value) != "0") {
            options.emplace_back("verbose");
        }
    }
    if (const char *value = std::getenv("TMPLGEN_COMPILED")) {
        if (value[0] != '\0' && std::string(value) != "0") {
            options.emplace_back("compiled");
        }
    }
    if (const char *value = std::getenv("TMPLGEN_NO_CACHE")) {
        if (value[0] != '\0' && std::string(value) != "0") {
            options.emplace_back("no-cache");
        }
    }
    if (const char *value = std::getenv("TMPLGEN_EMIT_LINE")) {
        if (value[0] != '\0' && std::string(value) != "0") {
            options.emplace_back("emit-line");
        }
    }
}

inline void appendClangArg(std::vector<std::string> &options,
                           llvm::StringRef argument)
{
    if (!argument.empty()) {
        options.emplace_back("clang-arg=" + argument.str());
    }
}

inline llvm::StringRef includeDirectoryFlag(clang::frontend::IncludeDirGroup group)
{
    using clang::frontend::IncludeDirGroup;
    switch (group) {
    case IncludeDirGroup::Quoted: return "-iquote";
    case IncludeDirGroup::Angled: return "-I";
    case IncludeDirGroup::System:
    case IncludeDirGroup::ExternCSystem:
    case IncludeDirGroup::CSystem:
    case IncludeDirGroup::CXXSystem:
    case IncludeDirGroup::ObjCSystem:
    case IncludeDirGroup::ObjCXXSystem:
        return "-isystem";
    case IncludeDirGroup::After: return "-idirafter";
    }
    return "-I";
}

// Forward the compiler options that affect how project sources are understood.
// Without this, plugin users must duplicate include paths, defines, the target,
// and the language standard by hand; a mismatch also changes generated output.
inline void appendCompilerOptions(const clang::CompilerInstance &compiler,
                                  llvm::StringRef workingDirectory,
                                  std::vector<std::string> &options)
{
    const clang::LangOptions &language = compiler.getLangOpts();
    if (language.LangStd != clang::LangStandard::lang_unspecified) {
        const char *name = clang::LangStandard::getLangStandardForKind(
            language.LangStd).getName();
        if (name != nullptr && name[0] != '\0') {
            appendClangArg(options, std::string("-std=") + name);
        }
    }

    const clang::TargetOptions &target = compiler.getTargetOpts();
    if (!target.Triple.empty()) {
        appendClangArg(options, "--target=" + target.Triple);
    }

    const clang::HeaderSearchOptions &search =
        compiler.getHeaderSearchOpts();
    if (!search.ResourceDir.empty()) {
        appendClangArg(options,
                       "-resource-dir=" +
                           absolutePath(search.ResourceDir, workingDirectory));
    }
    if (!search.Sysroot.empty() && search.Sysroot != "/") {
        appendClangArg(options,
                       "--sysroot=" +
                           absolutePath(search.Sysroot, workingDirectory));
    }
    for (const clang::HeaderSearchOptions::Entry &entry :
         search.UserEntries) {
        if (entry.Path.empty()) continue;
        llvm::StringRef flag = entry.IsFramework
            ? (llvm::StringRef("-iframework"))
            : includeDirectoryFlag(entry.Group);
        std::string path = absolutePath(entry.Path, workingDirectory);
        appendClangArg(options, (flag + path).str());
    }

    const clang::PreprocessorOptions &preprocessor =
        compiler.getPreprocessorOpts();
    for (const auto &macro : preprocessor.Macros) {
        appendClangArg(options, (macro.second ? "-U" : "-D") + macro.first);
    }
}

inline bool appendOption(llvm::StringRef argument,
                         llvm::StringRef workingDirectory,
                         std::vector<std::string> &cliArguments,
                         bool &hasSourceRoot, bool &hasTemplateDir,
                         bool &hasOutput, bool &hasLinePathBase)
{
    (void)argument.consume_front("--");
    size_t equals = argument.find('=');
    llvm::StringRef name = equals == llvm::StringRef::npos
        ? argument : argument.take_front(equals);
    llvm::StringRef value = equals == llvm::StringRef::npos
        ? llvm::StringRef() : argument.drop_front(equals + 1U);
    const char *option = nullptr;
    bool pathOption = false;

    if (name == "source-root") {
        option = "--source-root";
        pathOption = true;
        hasSourceRoot = true;
    } else if (name == "template-dir") {
        option = "--template-dir";
        pathOption = true;
        hasTemplateDir = true;
    } else if (name == "output") {
        option = "--output";
        pathOption = true;
        hasOutput = true;
    } else if (name == "line-path-base") {
        option = "--line-path-base";
        pathOption = true;
        hasLinePathBase = true;
    } else if (name == "dispatch") {
        option = "--dispatch";
    } else if (name == "reflect") {
        if (equals == llvm::StringRef::npos || value.empty()) {
            llvm::errs() << "tmplgen: plugin argument 'reflect' requires a value\n";
            return false;
        }
        cliArguments.push_back("--reflect=" + value.str());
        return true;
    } else if (name == "clang-arg") {
        option = "--clang-arg";
    } else if (name == "check" || name == "compiled" ||
               name == "no-cache" || name == "verbose" ||
               name == "emit-line" || name == "no-emit-line") {
        if (equals != llvm::StringRef::npos) {
            llvm::errs() << "tmplgen: plugin flag '" << name
                         << "' does not take a value\n";
            return false;
        }
        cliArguments.push_back("--" + name.str());
        return true;
    } else {
        llvm::errs() << "tmplgen: unknown plugin argument '" << argument << "'\n"
                     << "expected source-root=DIR, template-dir=DIR, output=FILE, "
                        "line-path-base=DIR, dispatch=MODE, clang-arg=ARG, "
                        "or a flag\n";
        return false;
    }

    if (equals == llvm::StringRef::npos || value.empty()) {
        llvm::errs() << "tmplgen: plugin argument '" << name
                     << "' requires a value\n";
        return false;
    }
    cliArguments.push_back(option);
    cliArguments.push_back(pathOption
        ? absolutePath(value, workingDirectory) : value.str());
    return true;
}

inline llvm::StringRef optionValue(const std::vector<std::string> &arguments,
                                   llvm::StringRef option)
{
    for (size_t i = arguments.size(); i > 1U; --i) {
        if (arguments[i - 2U] == option) return arguments[i - 1U];
    }
    return {};
}

inline bool findTemplateDirectory(llvm::StringRef startDirectory,
                                  std::string &projectRoot,
                                  std::string &templateDirectory,
                                  bool &testLayout)
{
    llvm::SmallString<256> current(startDirectory);
    for (;;) {
        llvm::SmallString<256> candidate(current);
        llvm::sys::path::append(candidate, "tmpl");
        if (llvm::sys::fs::is_directory(candidate)) {
            projectRoot = current.str().str();
            templateDirectory = candidate.str().str();
            testLayout = false;
            return true;
        }

        candidate = current;
        llvm::sys::path::append(candidate, "test", "tmpl");
        if (llvm::sys::fs::is_directory(candidate)) {
            projectRoot = current.str().str();
            templateDirectory = candidate.str().str();
            testLayout = true;
            return true;
        }

        llvm::SmallString<256> parent(llvm::sys::path::parent_path(current));
        if (parent.empty() || parent == current) return false;
        current = parent;
    }
}

// How the main file's text is handed to the generator.
enum class MainFileText {
    // Use the compiler's main-file buffer (may contain unsaved edits).
    CompilerBuffer,
    // Use `mainFileContents` instead of the compiler buffer.
    Override,
    // Let the generator read the main file from disk.
    Disk,
};

inline int runTmplgen(const clang::CompilerInstance &compiler,
                      const std::vector<std::string> &pluginArguments,
                      MainFileText mainFileText = MainFileText::CompilerBuffer,
                      llvm::StringRef mainFileContents = {})
{
    std::string workingDirectoryStorage = compilerWorkingDirectory(compiler);
    llvm::StringRef workingDirectory = workingDirectoryStorage;

    std::vector<std::string> options;
    appendCompilerOptions(compiler, workingDirectory, options);
    appendEnvironmentOptions(options);
    options.insert(options.end(), pluginArguments.begin(), pluginArguments.end());

    std::vector<std::string> arguments;
    arguments.emplace_back("tmplgen-plugin");
    bool hasSourceRoot = false;
    bool hasTemplateDir = false;
    bool hasOutput = false;
    bool hasLinePathBase = false;
    for (const std::string &option : options) {
        if (!appendOption(option, workingDirectory, arguments,
                          hasSourceRoot, hasTemplateDir, hasOutput,
                          hasLinePathBase)) {
            return 2;
        }
    }
    llvm::StringRef configuredRoot = optionValue(arguments, "--source-root");
    llvm::StringRef configuredTemplateDir = optionValue(arguments, "--template-dir");
    llvm::StringRef configuredOutput = optionValue(arguments, "--output");
    std::string projectRoot;
    std::string templateDirectory;
    bool testLayout = false;

    if (hasTemplateDir) {
        templateDirectory = configuredTemplateDir.str();
        projectRoot = hasSourceRoot ? configuredRoot.str() : workingDirectory.str();
    } else {
        llvm::StringRef searchRoot = hasSourceRoot ? configuredRoot : workingDirectory;
        if (!findTemplateDirectory(searchRoot, projectRoot,
                                   templateDirectory, testLayout)) {
            // A globally loaded plugin should quietly ignore unrelated projects.
            return 0;
        }
    }

    if (!hasSourceRoot) {
        arguments.emplace_back("--source-root");
        arguments.push_back(projectRoot);
    }
    if (!hasTemplateDir) {
        arguments.emplace_back("--template-dir");
        arguments.push_back(templateDirectory);
    }
    // The standalone tool is conventionally run from the project root with a
    // relative template directory, which yields root-relative #line paths.
    // Produce the same output here instead of absolute paths, unless the
    // project root is only a guess (template dir configured without a root).
    if (!hasLinePathBase && (hasSourceRoot || !hasTemplateDir)) {
        arguments.emplace_back("--line-path-base");
        arguments.push_back(projectRoot);
    }
    if (!hasOutput) {
        arguments.emplace_back("--output");
        if (testLayout) {
            arguments.push_back(absolutePath("test/gen/all.gen.h", projectRoot));
        } else {
            arguments.push_back(absolutePath("gen/all.gen.h", projectRoot));
        }
    }

    std::vector<char *> argv;
    argv.reserve(arguments.size());
    for (std::string &argument : arguments) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }

    llvm::StringRef outputPath;
    for (size_t i = 0U; i + 1U < arguments.size(); ++i) {
        if (arguments[i] == "--output") {
            outputPath = arguments[i + 1U];
            break;
        }
    }
    if (outputPath.empty()) {
        llvm::errs() << "tmplgen: plugin invocation has no output path\n";
        return 2;
    }
    llvm::SmallString<256> lockName(llvm::sys::path::parent_path(outputPath));
    llvm::sys::path::append(lockName, ".tmplgen-plugin");
    if (std::error_code error = llvm::sys::fs::create_directories(
            llvm::sys::path::parent_path(lockName))) {
        llvm::errs() << "tmplgen: cannot create plugin output directory: "
                     << error.message() << '\n';
        return 1;
    }
    llvm::LockFileManager lock(lockName);
    for (;;) {
        auto acquired = lock.tryLock();
        if (!acquired) {
            llvm::errs() << "tmplgen: unable to acquire plugin generation lock: "
                         << llvm::toString(acquired.takeError()) << '\n';
            return 1;
        }
        if (*acquired) break;
        (void)lock.waitForUnlockFor(std::chrono::seconds(30));
    }
    tmplgen_plugin_clear_unsaved_source();
    if (mainFileText != MainFileText::Disk && compiler.hasSourceManager()) {
        const clang::SourceManager &sourceManager = compiler.getSourceManager();
        clang::FileID mainFile = sourceManager.getMainFileID();
        if (mainFile.isValid()) {
            bool invalid = false;
            llvm::StringRef contents = mainFileText == MainFileText::Override
                ? mainFileContents
                : sourceManager.getBufferData(mainFile, &invalid);
            llvm::StringRef path = sourceManager.getFilename(
                sourceManager.getLocForStartOfFile(mainFile));
            if (!invalid && !path.empty()) {
                std::string pathStorage = path.str();
                tmplgen_plugin_set_unsaved_source(pathStorage.c_str(),
                                                   contents.data(),
                                                   contents.size());
            }
        }
    }
    int result = tmplgen_plugin_main(static_cast<int>(argv.size()), argv.data());
    tmplgen_plugin_clear_unsaved_source();
    return result;
}

} // namespace tmplgen_plugin_support

#endif
