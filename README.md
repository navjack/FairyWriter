# FairyWriter

FairyWriter is an experimental word processor whose complete 256x224 interface
runs as a source-generated Super Nintendo cartridge inside a native Qt
application. The cartridge owns what you see and how input feels; the native
host owns documents, files, recovery, and operating-system integration.

The project is now open for development. It is usable enough for focused
testing, but it is not yet a stable general release. Expect missing features,
rough edges, and file-format limitations.

[Download the current tester packages from GitHub
Releases](https://github.com/navjack/FairyWriter/releases). Release numbering
and the publication checklist are documented in
[VERSIONING.md](VERSIONING.md).

No commercial ROM or copyrighted game data is included. The 64 KiB FairyWriter
cartridge is generated from source during every build and embedded in the
application.

## What works today

- cartridge-owned editor, menus, file browser, help, caret, selection, toolbar,
  and document-position control;
- keyboard input through an emulated XBAND keyboard and pointer input through
  an emulated Super NES Mouse;
- native document state, undo/redo, crash recovery, recent files, and atomic
  saves;
- ODT, DOCX, RTF, HTML, and plain-text document paths inherited and adapted
  from FocusWriter;
- macOS arm64, Linux x86_64, and Windows x64 release packages built by the same
  three-platform production gate; Windows still awaits physical-host acceptance;
- real cartridge/runtime, mailbox, document, recovery, and packaging tests.

The fixed 256x224 virtual screen and cartridge ownership of all visible UI are
product invariants, not a temporary theme.

## Running a release build

### Linux x86_64

Downloading over HTTP does not preserve the executable bit, so set it once:

```sh
chmod +x FairyWriter-*-x86_64.AppImage
./FairyWriter-*-x86_64.AppImage
```

If your file manager offers to open the download with **Disk Image Writer**,
**Disk Image Mounter**, or an archive manager, that is the missing executable
bit and not a damaged file. An AppImage is reported as
`application/vnd.appimage`, which the shared MIME database declares a subtype of
`application/vnd.squashfs`, so a file manager that cannot execute the file falls
back to a disk-image tool instead. Run the `chmod` above, or right-click the
file and use Properties, Permissions, *Allow executing file as program*.

Once the bit is set, Cinnamon, MATE, and Xfce run the application on
double-click, and KDE Plasma asks first whether to run or open it. GNOME Files
does not launch binaries on double-click at all: start the AppImage from a
terminal there. Its *Run as a Program* right-click action also works, but only
when GNOME Console is installed, and it fails silently otherwise.

`libfuse2` is not required. The AppImage runtime links FUSE statically and uses
the `fusermount` already provided by the `fuse3` package on Linux Mint 21 and
newer, Ubuntu 22.04 and newer, Fedora, and Arch. Where FUSE is genuinely
unavailable, such as inside a container or under a hardened kernel, run the
extraction fallback:

```sh
./FairyWriter-*-x86_64.AppImage --appimage-extract-and-run
```

### macOS arm64 and Windows x64

Per-platform launch steps, including the Gatekeeper quarantine and SmartScreen
prompts, are in [TESTING.md](TESTING.md).

## Build

FairyWriter needs CMake, Ninja, Go, Qt 6.8 or newer, Zlib, and a C/C++ compiler.
First prepare the pinned runtime dependency:

```sh
./scripts/bootstrap-snesrecomp.sh
```

Then configure, build, and test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [BUILDING.md](BUILDING.md) for platform-specific requirements and
packaging. `VERSION` is the authoritative product/package version.

## How it works

The build generates a real LoROM image from `tools/fairywriter-rom`, embeds it
in the native executable, and runs it through the pinned `snesrecomp` machine
core. Commands and viewport snapshots cross a bounded SRAM mailbox:

```text
keyboard / mouse -> cartridge -> command ring -> native document engine
                                           <-> viewport snapshot buffers
screen pixels     <- cartridge <- metadata / text / format runs
```

This boundary lets the cartridge remain the authoritative user interface while
the host safely handles full documents and platform services. Read
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the detailed explanation.

## Project map

- `tools/fairywriter-rom/` — deterministic 65816 cartridge generator
- `src/snes_machine.c` — embedded SNES machine boundary
- `src/mailbox.*` — bounded cartridge/host protocol
- `src/document_engine.*` — authoritative native document model
- `packaging/` — macOS, Linux, and Windows packaging gates
- `tests/` — real protocol, document, presentation, and machine regressions
- `plan.md` and `DEVELOPMENT_STATUS.md` — active roadmap and implementation
  status

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md). The active engineering state is
kept in `SCRATCH.MD` so work can survive agent and developer handoffs; please
preserve and update it when making substantial changes.

Bug reports and development discussion are welcome in
[GitHub Issues](https://github.com/navjack/FairyWriter/issues).

## Development process and AI disclosure

FairyWriter was conceived and directed by Jack Mangano. The product ideas came
from Jack, with the initial spark coming from a friend who wanted a word
processor that looked like it belonged on the SNES. The decisive answer was to
build the word processor on an actual source-generated SNES cartridge.

Development made extensive use of agentic LLM coding assistance from Claude
Fable, Opus, and Sonnet; Gemini Flash and Pro; and ChatGPT 5.6 Luna, Terra, and
Sol. The agents helped implement, investigate, test, document, package, and
audit the project under human direction. Read
[How FairyWriter was developed](docs/DEVELOPMENT_PROCESS.md) for the full
history, workflow, and attribution.

## Heritage and license

FairyWriter is derived from
[FocusWriter 1.9.0](https://gottcode.org/focuswriter/). Its document and desktop
foundation is copyright Graeme Gott and contributors. See
[CREDITS.md](CREDITS.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and
the retained [upstream changelog](docs/UPSTREAM_CHANGELOG.md).

FairyWriter is free software under [GPL-3.0-or-later](COPYING).
