# Building

Requirements: a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022, Apple Clang 14+), CMake 3.24+, Git, and an internet
connection for the first configure (dependencies are fetched at pinned versions).

| Dependency | Pinned version | How it is used |
|---|---|---|
| Dear ImGui | `v1.91.5` | UI |
| SDL2 | `release-2.30.9` | window, input, OpenGL context (built statically) |
| portable-file-dialogs | `0.1.0` | native file dialogs; **headers only**, never configured as a CMake project |

## Linux

```sh
sudo apt-get install build-essential cmake git libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev \
     libxcursor-dev libxi-dev libxinerama-dev libxss-dev libxkbcommon-dev
cmake -S sm64-ultimate-save-editor -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/sm64-ultimate
```

For native file dialogs install `zenity` (or `kdialog`).

## Windows (Visual Studio 2022)

```bat
cmake -S sm64-ultimate-save-editor -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\Release\sm64-ultimate.exe
```

`NOMINMAX` and `WIN32_LEAN_AND_MEAN` are defined for every target by CMake, so `std::min`/`std::max` are safe.

## macOS

```sh
cmake -S sm64-ultimate-save-editor -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Options

| Option | Default | Meaning |
|---|---|---|
| `SM64_BUILD_GUI` | `ON` | build the SDL2 application (turn `OFF` for engine + tests only; needs no SDL2/OpenGL) |
| `SM64_BUILD_TESTS` | `ON` | build the test executables |
| `SM64_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |
| `SM64_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` for this project's code (not third-party code) |

## Tests

| Test | What it covers |
|---|---|
| `core` | layout, checksums (independent reference vectors), edits, repair, presets, history, atomic writes, exports, GameShark, reports, settings |
| `ui-ids` | headless Dear ImGui: duplicate-widget-ID audit of every page/dialog in both modes, themes and sizes; popup bounds; app workflow (open/save/unsaved guards/backups/read-only/shortcuts) |

The UI test builds a second copy of Dear ImGui with `IMGUI_ENABLE_TEST_ENGINE` so it can see every widget ID submitted in a
frame. No display or GPU is needed.

## Troubleshooting

* *`CMP0169`/"policy" errors from portable-file-dialogs*: not applicable; it is fetched with `SOURCE_SUBDIR` pointing at a
  directory without a `CMakeLists.txt`, so its outdated CMake project is never configured.
* *Offline builds*: pass `-DFETCHCONTENT_SOURCE_DIR_IMGUI=...`, `-DFETCHCONTENT_SOURCE_DIR_SDL2=...`,
  `-DFETCHCONTENT_SOURCE_DIR_PFD=...` pointing at local checkouts of the pinned versions.
