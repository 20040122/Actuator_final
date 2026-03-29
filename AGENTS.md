# Repository Guidelines

## Project Structure & Module Organization
- `src/main.cpp` is the entry point and builds into the `main` executable.
- Core implementation is grouped by responsibility: `src/coordinator`, `src/executor`, `src/parser`, `src/constraint`, and `src/core`.
- Sample runtime inputs live in `src/input/` as JSON files such as `behaviorTree.json` and `schedule.json`.
- Third-party headers are vendored under `src/third_party/nlohmann`; treat this as external code and avoid local style changes there.

## Build, Test, and Development Commands
- Configure: `cmake -S . -B build -G Ninja` creates a local build directory.
- Build: `cmake --build build` compiles the `main` target defined in `CMakeLists.txt`.
- Run on Unix: `MS_LOG_LEVEL=debug ./build/main` starts the program with verbose logging.
- Run on Windows PowerShell: `$env:MS_LOG_LEVEL="debug"; .\build\main.exe`.
- If CMake cache becomes stale, remove `build/CMakeCache.txt` and `build/CMakeFiles/` before reconfiguring.

## Coding Style & Naming Conventions
- Follow the existing C++11 codebase style: 4-space indentation, braces on the same line, and header/source pairs such as `coordinator.h` and `coordinator.cpp`.
- Use `snake_case` for files, variables, and functions; use concise PascalCase only for types when already established.
- Keep module boundaries clear: parsing logic stays in `src/parser`, execution logic in `src/executor`, and communication logic in `src/coordinator`.
- Prefer standard library facilities and the vendored `nlohmann/json` dependency instead of adding new libraries.

## Testing Guidelines
- There is no dedicated automated test suite yet. Validate changes by rebuilding and running `./build/main` against the sample JSON inputs in `src/input/`.
- For parser or scheduler changes, add or update representative JSON fixtures and document the scenario in the PR.
- When adding tests, place them under a top-level `tests/` directory and register them with CMake so they can run through `ctest`.

## Commit & Pull Request Guidelines
- Recent history uses short, task-focused commits in either Chinese or English, for example `docs: 添加项目开发计划` or `0316：版本大更新`.
- Keep commit messages brief and specific; prefer one logical change per commit.
- PRs should describe the behavioral change, list affected modules, and include build/run verification steps.
- Include sample input/output or screenshots when a change affects logs, scheduling output, or platform-specific behavior.
