# Clang and clangd plugin modes

The ordinary `tmplgen` executable remains the portable standalone mode. The
optional plugin build reuses the same C generator core but adds thin C++ adapters
for each host:

- A **Clang frontend plugin** registers a `PluginASTAction` and runs generation
  before Clang parses the translation unit.
- A **clangd feature-module plugin** registers a `FeatureModule` and runs
  generation before clangd builds a main-file AST.

These are separate plugin libraries because Clang and clangd have different
plugin registries and APIs. The shared C core is compiled with `-DCLANGPLUGIN`
to export a callable entry point instead of defining `main()`.

## Build the Clang plugin

Install matching LLVM/Clang development packages, including Clang’s C++ frontend
libraries, then configure the optional plugin target:

```sh
cmake -S plugin -B build/plugin \
  -DLLVM_DIR="$(llvm-config --cmakedir)" \
  -DClang_DIR=/path/to/lib/cmake/clang \
  -DBUILD_CLANG_PLUGIN=ON
cmake --build build/plugin --target tmplgen-clang-plugin
```

The output is `build/plugin/libtmplgen.so`. It must be built against the same
Clang version that loads it: the C++ plugin ABI is not stable across LLVM
versions.

Load it with the Clang driver:

```sh
clang \
  -fplugin=/absolute/path/build/plugin/libtmplgen.so \
  -fplugin-arg-tmplgen-source-root=/path/to/project \
  -fplugin-arg-tmplgen-template-dir=/path/to/project/tmpl \
  -fplugin-arg-tmplgen-output=/path/to/project/gen/all.gen.h \
  -fplugin-arg-tmplgen-clang-arg=-std=c11 \
  -c /path/to/project/src/main.c
```

Plugin arguments use `key=value` after the `-fplugin-arg-tmplgen-` prefix. The
supported keys are `source-root`, `template-dir`, `output`, `line-path-base`,
`dispatch`, `reflect` (`none` or `project`), and repeatable `clang-arg`; the flags `compiled`, `no-cache`, `verbose`, `emit-line`,
`no-emit-line`, and `check` are also accepted. If paths are omitted, the plugin
searches upward from the compilation command’s working directory for a project
template directory. It recognizes both the normal `tmpl/` layout and this
repository’s `test/tmpl/` fixture layout; if no templates are found, it does
nothing. Explicit plugin arguments override environment variables and discovered
paths. Template
`#line` paths are written relative to the project root (as if the standalone
tool ran there with a relative `--template-dir`) unless `line-path-base` says
otherwise; this is skipped when only `template-dir` is configured, because the
root would be a guess.

Both adapters automatically forward the active compiler invocation’s language
standard, target triple, sysroot, resource directory, include search paths, and
command-line macro definitions/undefinitions to the generator’s libclang parse.
Use `clang-arg` (Clang) or `TMPLGEN_CLANG_ARGS` (either adapter) only for extra
options that are not represented in that invocation, such as project-specific
feature flags. Relative include paths are anchored at the compile command’s
working directory, matching Clang’s lookup. This keeps template analysis aligned
with the translation unit without duplicating the build system’s flags by hand.

The plugin invokes the same project-wide source scan as the standalone tool.
It runs after Clang loads the main file but before parsing it, so the current
in-memory main-file buffer is used and generated headers are ready for that
compilation. It does not replace dependency tracking in the build system:
changing a template still needs to trigger a compile command or a standalone
generation step. Plugin invocations targeting the same aggregate output are
serialized, including concurrent Clang processes.

## Build the clangd plugin

clangd does not load a Clang `PluginASTAction` from the compilation database.
The clangd adapter is a separate `FeatureModule` plugin, loaded explicitly by a
clangd build with dynamic plugin support. Dynamic `FeatureModule` loading landed
upstream after clangd 22.1.5 and is gated by `CLANG_PLUGIN_SUPPORT`, which in
turn depends on `LLVM_ENABLE_PLUGINS`. Distribution binaries may be built
without it; verify that `clangd --help-hidden` lists `--load` before trying to
load a module.

Build clangd from a matching LLVM/Clang source tree with plugin support enabled,
for example:

```sh
cmake -S /path/to/llvm-project/llvm -B /path/to/llvm-build \
  -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' \
  -DLLVM_ENABLE_PLUGINS=ON
cmake --build /path/to/llvm-build --target clangd
```

Then build the feature-module library against that same build and source tree:

```sh
cmake -S plugin -B build/clangd-plugin \
  -DLLVM_DIR=/path/to/llvm-build/lib/cmake/llvm \
  -DClang_DIR=/path/to/llvm-build/lib/cmake/clang \
  -DBUILD_CLANG_PLUGIN=OFF \
  -DBUILD_CLANGD_PLUGIN=ON \
  -DCLANGD_SOURCE_DIR=/path/to/llvm-project/clang-tools-extra/clangd
cmake --build build/clangd-plugin --target tmplgen-clangd-plugin
```

Load the module explicitly:

```sh
/path/to/llvm-build/bin/clangd \
  -load=/absolute/path/build/clangd-plugin/libtmplgen-clangd.so
```

The clangd module runs before main-file AST construction, serializing generator
calls and using the existing content caches to make unchanged parses cheap. It
searches upward from the compile command’s working directory for `tmpl/` or
`test/tmpl/`, and quietly skips work when neither is present. The active main
file is analyzed from clangd’s current buffer contents, including unsaved edits;
other project files and templates are read from disk. clangd must support
dynamic plugins, and the module must be built against the exact LLVM/Clang
version used by that clangd binary. `clangd --check` does not pass feature
modules through to AST parsing, so it does not exercise generation or the
adapter’s editor features; test those through an editor or an LSP session.

Configure either adapter with environment variables when its defaults do not
match the workspace. Explicit Clang plugin arguments take precedence:

| Variable | Purpose |
| --- | --- |
| `TMPLGEN_SOURCE_ROOT` | Project tree to scan |
| `TMPLGEN_TEMPLATE_DIR` | Directory containing `*.tmpl.h` files |
| `TMPLGEN_OUTPUT` | Aggregate generated header path |
| `TMPLGEN_DISPATCH` | Dispatch backend: `auto`, `generic`, `gnu`, or `none` |
| `TMPLGEN_LINE_PATH_BASE` | Directory that template `#line` paths are relative to |
| `TMPLGEN_REFLECT` | `project` to also write `gen/reflection.h` (see `doc/usage.md`) |
| `TMPLGEN_CLANG_ARGS` | Newline-separated arguments passed to libclang |
| `TMPLGEN_COMPILED` | Set to a nonzero value to emit `.gen.c` files |
| `TMPLGEN_NO_CACHE` | Set to a nonzero value to disable caches |
| `TMPLGEN_EMIT_LINE` | Set to a nonzero value to map generated bodies to templates |
| `TMPLGEN_VERBOSE` | Set to a nonzero value for generator logging |

## Neovim configuration

Neovim must launch clangd with the FeatureModule library. It is a clangd
**server argument**, not a compiler flag from `compile_commands.json` or a
project `.clangd` file. A basic Neovim 0.11+ setup is:

```lua
vim.lsp.config("clangd", {
  cmd = {
    "/path/to/clangd",
    "-load=/absolute/path/to/libtmplgen-clangd.so",
  },
})
vim.lsp.enable("clangd")
```

For a plugin used across multiple workspaces, keep it on the clangd command and
let the module no-op when it cannot find a template directory. To scope it to a
single project, select the `-load` argument based on clangd’s `root_dir` in the
editor configuration. After changing the command, restart Neovim or restart the
clangd client so the shared library is loaded by a new server process.

Use the standalone executable in build systems that need predictable dependency
tracking, unsaved-buffer support, or a clangd version without dynamic plugin
loading. Plugin mode is an additional integration path, not a replacement for
the standalone one.
