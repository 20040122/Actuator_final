```shell
Remove-Item -Recurse -Force e:\Codex\build\CMakeCache.txt, e:\Codex\build\CMakeFiles

MS_LOG_LEVEL=debug ./build/main

$env:MS_LOG_LEVEL = "debug"; .\build\main.exe
```


## 构建信息
- **UNIX**: clang 17.0.0
- **Windows**: g++ 15.2.0
- **CMake**: 4.2.3
- **Ninja**: 1.13.1
- **C++ 标准**: C++11
- **第三方库**: nlohmann/json (header-only)