# mk61emu

```sh
cmake --preset gui-local
cmake --build --preset gui-local
ctest --preset gui-local
cmake --preset gui-release-local
cmake --build --preset gui-release-local
```

English | [Українська](README.uk.md)

A small C++23 template with a core library, CLI application, and Catch2 tests.
An optional Qt Widgets application is available under `apps/gui`.
No CI configuration or application-specific dependencies are included.

## Requirements

- CMake 3.25 or newer, Ninja, and Git.
- A compiler and standard library supporting C++23, including `<format>` and
  `<print>`. Selecting C++23 alone does not provide missing library features.
  The current setup has been tested with GCC 16 and libstdc++ 16 on Linux;
  other compiler/library combinations must be verified before use.
- Optional: clangd, clang-format, and clang-tidy, compatible with the selected
  compiler and standard library. The analysis tools were tested at version 22.

Catch2 v3.5.2 is fetched during configuration when tests are enabled. The first
configuration needs network access. To use an existing checkout, pass
`-DFETCHCONTENT_SOURCE_DIR_CATCH2=/absolute/path/to/Catch2`.

## Build and Test

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/apps/cli/mk61emu

cmake --preset release
cmake --build --preset release
```

Release builds omit tests by default. Both presets use Ninja, so Windows users
need a configured compiler environment, such as a Visual Studio developer shell.

## Checks

```sh
cmake --build build/dev --target format-check
cmake --build build/dev --target format

cmake --preset sanitized
cmake --build --preset sanitized
ctest --preset sanitized

cmake --preset analyze
cmake --build --preset analyze
ctest --preset analyze
```

`format` changes project C++ sources; `format-check` only checks them. These
targets exist when clang-format is found at configure time. Formatting does not
include generated files or downloaded dependencies.

The sanitized preset enables ASan and UBSan on project targets. It requires GCC
or Clang on a Unix-like platform and the corresponding sanitizer runtimes.
Dependencies are not instrumented by these project options. TSan is not bundled;
it should be added separately when concurrent code needs it.

The analyze preset requires clang-tidy and checks project targets, not Catch2's
own sources. Enabled clang-tidy findings fail the build. Override tool discovery
with `-DCLANG_TIDY_EXECUTABLE=/path/to/clang-tidy` or
`-DCLANG_FORMAT_EXECUTABLE=/path/to/clang-format`, then reconfigure.

Project options, all configurable with `-DNAME=ON` or `OFF`:

| Option                      | Default | Purpose                                    |
| --------------------------- | ------- | ------------------------------------------ |
| `BUILD_APPS`                | ON      | Build the CLI                              |
| `BUILD_GUI`                 | OFF     | Build the optional Qt Widgets application  |
| `BUILD_TESTS`               | ON      | Fetch Catch2 and build tests               |
| `ENABLE_WARNINGS_AS_ERRORS` | OFF     | Fail on project compiler warnings          |
| `ENABLE_SANITIZERS`         | OFF     | Enable ASan and UBSan                      |
| `ENABLE_CLANG_TIDY`         | OFF     | Analyze project sources during compilation |

The sanitized and analyze presets also enable warnings-as-errors. Basic compiler
warnings apply to project tests; stricter warnings apply to the library and CLI.
Catch2's own source compilation has warnings disabled (`-w` or `/w`), and its
headers are treated as system headers.

## Optional Qt Widgets Application

Qt is only searched for when `BUILD_GUI=ON`. The library, CLI, and tests do not
depend on Qt; `BUILD_APPS` and `BUILD_GUI` are independent. Install a Qt 6
development kit that includes Widgets and the moc/uic/rcc tools. On Debian or
Ubuntu, the system development package is typically `qt6-base-dev`. Qt is not
downloaded by this project.

For a system installation:

```sh
cmake --preset gui
cmake --build --preset gui
ctest --preset gui
./build/gui/apps/gui/mk61emu-gui
```

The launch path above is for Linux. On Windows the executable has an `.exe`
suffix; on macOS it is inside an application bundle. The example displays a
greeting from the core library and updates it when the name changes.

For a Qt kit installed in a custom directory:

```sh
cmake --preset gui -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/gcc_64"
cmake --build --preset gui
```

`CMAKE_PREFIX_PATH` points to the kit's installation prefix, not its `include`
or `bin` directory. CMake locates Qt's package configuration and imported targets,
which provide the include paths, libraries, and tools. Alternatively, pass
`-DQt6_DIR="$HOME/Qt/6.12.0/gcc_64/lib/cmake/Qt6"`. Usually only one of these
settings is needed. The version and path above are examples, not requirements.

To keep your installation path out of shared configuration, create the ignored
`CMakeUserPresets.json` in the project root (or merge this preset into it):

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "gui-local",
            "inherits": "gui",
            "cacheVariables": {
                "CMAKE_PREFIX_PATH": "$env{HOME}/Qt/6.12.0/gcc_64"
            }
        }
    ],
    "buildPresets": [{ "name": "gui-local", "configurePreset": "gui-local" }]
}
```

Use `cmake --preset gui-local` and `cmake --build --preset gui-local`. This
example inherits `build/gui`, so the GUI clangd configuration works unchanged.
Use a separate build directory when switching Qt kits, architectures, or
compilers, and update the GUI database path in `.clangd` to match. Do not mix
system Qt components with components from another kit. The kit must match your
platform, architecture, and a compatible compiler/ABI.

The GUI links only `Qt6::Widgets`, which also supplies `Gui` and `Core`.
To add another module, install it in the same kit, add its name to
`find_package(Qt6 REQUIRED COMPONENTS Widgets Network)`, and link its target,
for example `Qt6::Network`, in `apps/gui/CMakeLists.txt`. There is no need to
link every Qt module.

Only the GUI target enables `AUTOMOC` (for `Q_OBJECT`, signals, and slots),
`AUTOUIC` (for included generated form headers), and `AUTORCC` (for resources).
List your custom headers, `.ui` forms, and `.qrc` resources in `qt_add_executable`
as you add them. Qt code generation does not affect the core library or Catch2.
The `gui` preset runs the existing core tests; it does not add automated GUI
tests. Packaging for another machine additionally requires Qt's runtime
libraries and plugins, including the platform plugin; deployment is not
implemented by this template.

## Editor Setup

Install the clangd extension and clangd 22 or newer. The shared settings select
`clangd-22` by executable name, without an absolute installation path. If your
installation uses a different name (for example `clangd`), adjust `clangd.path`
in `.vscode/settings.json`; keep machine-specific absolute paths out of commits.
Clangd 18 produces false diagnostics with this project's libstdc++ 16 setup.
The shared settings disable the competing Microsoft C++ IntelliSense engine;
the Microsoft extension can still provide debugging.

Run `cmake --preset dev` before opening C++ files. The `.clangd` configuration
reads `build/dev/compile_commands.json` for include paths and compiler flags.
For `apps/gui`, it reads `build/gui/compile_commands.json`; configure the `gui`
or `gui-local` preset before editing GUI sources. No manual Qt include paths or
stub headers are needed.
After changing toolchains, reconfigure and run **clangd: Restart language server**.
For a nonstandard GCC installation, set `clangd.arguments` with a narrowly scoped
`--query-driver=/absolute/path/to/compiler` in your user settings.

Machine-specific compilers, tool paths, and presets belong in the ignored
`CMakeUserPresets.json`. Use a separate build directory for each toolchain. If
the editor should analyze another build, update `.clangd` accordingly.

## Adapting the Template

### Generate a Project Copy

Run the generator from this template with Python 3.9 or newer (no extra packages):

```sh
python3 scripts/create_project.py
```

The interactive prompts ask for the destination, project name, C++ namespace,
version, application type, tests, Qt kit, compiler, and whether to verify the build.
You can also supply everything on the command line:

```sh
python3 scripts/create_project.py ../WeatherDesk --name WeatherDesk \
  --namespace weather --version 1.0.0 --kind both \
  --qt-prefix '$env{HOME}/Qt/6.12.0/gcc_64' --verify
```

`--kind` accepts `cli` (default), `gui`, `both`, or `library`. Tests are enabled
by default; use `--no-tests` to avoid fetching Catch2. `--cxx-compiler /path/to/g++`
selects a compiler. Repeat `--cache KEY=VALUE` for additional local CMake cache
variables, such as `--cache ENABLE_WARNINGS_AS_ERRORS=ON` or
`--cache FETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2`. Build mode flags are
controlled by `--kind` and `--tests`, not `--cache`. Omit `--qt-prefix` for system
Qt. Quote CMake environment macros with single quotes as shown above; `~/Qt/...`
is not expanded inside CMake presets, so use an absolute path or `$env{HOME}`.

The destination must not exist and must be outside the template. The generator
copies source files and editor configuration, renames the CMake project, core
alias, C++ namespace, window title, and executable files, and adjusts build
defaults. For `both`, the executables are `WeatherDesk` and `WeatherDesk-gui`.
Disabled components remain available in the source tree. Git history, build
directories, existing local presets, and the generator's own scripts/tests are
not copied. The source template is not modified; rerun its generator for another
project.

Each copy gets an ignored `CMakeUserPresets.json` with configure/build presets:
`dev-local` and `release-local`, or `gui-local` and `gui-release-local` for GUI
projects. Qt paths and compiler choices are stored only in this local file.
Debug has a matching test preset when tests are enabled; Release omits tests.
The generated READMEs start with commands for the selected presets, and clangd
uses the corresponding Debug database. Local presets must be recreated on other
machines because Git ignores them.

`--verify` configures and builds both local presets, runs enabled Debug tests,
and exits nonzero on failure. CMake, Ninja, a suitable C++23 compiler, Qt for GUI,
and network access or a local Catch2 checkout for tests are required. On failure,
the generated project remains available for diagnosis; fix it in place or choose
a new destination when generating again.

Generator checks, including optional real builds and headless GUI startup:

```sh
python3 -m unittest discover -s tests -p test_create_project.py -v
CPP_STARTER_INTEGRATION=1 CPP_STARTER_QT_PREFIX="$HOME/Qt/6.12.0/gcc_64" \
  python3 -m unittest discover -s tests -p test_create_project.py -v
```

### Continue With Application Code

1. If adapting manually, rename `mk61emu`, `mk61emu::Core`, and the
   `mk61::core` namespace. The generator already performs this step.
2. Replace the example Greeter, CLI, and test with your application code.
3. Keep library headers in `libs/core/include` and implementation in
   `libs/core/src`; add executables under `apps` and tests under `tests`.
4. Link new project targets to `Project::Options`, `Project::Warnings`, and,
   where appropriate, `Project::StrictWarnings`. Add new target names to the
   clang-tidy target list in the root CMake configuration.
5. Choose a license and document supported compiler/library versions for the
   new project. Update this README as commands and requirements change.

Install/export rules, packaging, coverage, benchmarks, generated API docs,
additional dependency managers, logging, and CLI parsing are deliberately left
to individual projects.
