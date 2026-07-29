# Building FairyWriter

FairyWriter builds its cartridge from source and links a pinned, locally
patched `snesrecomp` runtime. A fresh clone does not need a separate ROM.
Package names and executable metadata read the canonical SemVer number from
`VERSION`.

## Requirements

- CMake 3.16 or newer
- Ninja
- Go
- Qt 6.8 or newer with Core, Gui, OpenGLWidgets, Widgets, and Test
- Zlib
- a C17/C++17-capable compiler
- Git, for the pinned runtime bootstrap

Prepare the runtime once:

```sh
./scripts/bootstrap-snesrecomp.sh
```

The script clones the exact tested `navjack/snesrecomp` revision under
`third_party/`, verifies ownership and cleanliness, and applies FairyWriter's
checked-in XBAND keyboard and Super NES Mouse patch. It refuses to overwrite
local changes.

## macOS arm64

Install Qt, CMake, Ninja, and Go with Homebrew. On Apple Silicon, use the native
Homebrew CMake and Qt rather than translated Intel tools:

```sh
/opt/homebrew/bin/cmake -S . -B build-arm64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
  -DBUILD_TESTING=ON
/opt/homebrew/bin/cmake --build build-arm64 --parallel
ctest --test-dir build-arm64 --output-on-failure
```

Create the tester DMG only after the tests pass:

```sh
./mac_deploy.sh
```

## Linux x86_64

The supported package path uses Docker to build against the project's pinned
Ubuntu/Qt baseline and then exercises both X11 and Wayland launch paths:

```sh
packaging/linux/build-linux.sh
```

The AppImage and checksum are written to `release-linux-x86_64/`.

## Windows x64

Install Visual Studio 2022, Qt 6.8 for MSVC x64, CMake, Go, Git, and vcpkg.
Run the bootstrap from Git Bash, then package from PowerShell:

```powershell
.\packaging\windows\build-windows.ps1 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -VcpkgRoot C:\vcpkg
```

The GitHub Windows x64 job runs this same MSVC build, production test gate,
deployment, package audit, and checksum path. The resulting ZIP still requires
its first physical-machine acceptance pass.

## Useful options

- `FAIRYWRITER_DEVELOPMENT_UI=ON` builds the retired FocusWriter-derived Qt UI
  for comparison; it is not the shipping product.
- `FAIRYWRITER_DEVELOPER_TOOLS=ON` builds standalone cartridge hosts.
- `FAIRYWRITER_SNESRECOMP_ROOT=/path` selects an already prepared runtime.

The production configuration currently registers 10 tests. The development
UI adds three legacy presentation tests. Automated success does not replace the
manual keyboard, mouse, recovery, and physical-platform checks in `TESTING.md`.
