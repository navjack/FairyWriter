# How FairyWriter works

FairyWriter is two programs cooperating inside one desktop process:

1. a real, source-generated Super Nintendo cartridge owns the interface and
   editing gestures;
2. a native Qt host owns the full document, filesystem, recovery, and window.

That division is the central design decision. FairyWriter is not a Qt editor
painted to look like a console game, and it is not a normal ROM that somehow
accesses the user's files directly.

## From source to application

`tools/fairywriter-rom` is a deterministic Go program that emits a 64 KiB
two-bank LoROM image. Bank 0 holds the 65816 program and the XBAND scan map;
bank 1 holds the palette, tilemap, static planes and PPU tiles. It assembles the cartridge program, resident font, palettes,
tilemaps, scene artwork, and startup vectors directly into bytes. Tests inspect
the generated machine code and data contracts.

CMake then:

1. runs the generator to create `fairywriter.sfc` inside the build directory;
2. runs `tools/embed-rom` to turn those bytes into a C++ source array;
3. links that array into the native application;
4. runs the cartridge through the pinned `snesrecomp` 65816/SNES machine core.

The loose `.sfc` is only an intermediate build artifact. Packaged applications
contain the embedded image and must not ship a separate ROM.

## The ownership boundary

The cartridge owns everything visible on the fixed 256x224 virtual screen:

- pixels, palettes, tiles, panels, toolbar, menus, browser, and help;
- caret, selection, document-position thumb, and proofing presentation;
- keyboard/mouse gesture interpretation and optimistic immediate feedback.

The host owns state that cannot safely or practically live in a small
cartridge:

- the authoritative `QTextDocument`;
- complete text, formatting, undo/redo, and statistics;
- ODT, DOCX, RTF, HTML, and plain-text parsing/writing;
- filesystem paths, recent-file catalog, atomic saves, and recovery;
- the native window, graphics upload, clipboard, and application lifecycle.

The host may provide data to the cartridge, but it may not quietly replace
cartridge UI with native widgets. Conversely, the cartridge never receives
arbitrary filesystem access or an unbounded copy of the document.

## The SRAM mailbox

The two sides communicate through a fixed-layout block of emulated cartridge
SRAM. `src/mailbox.*` defines the host view and `tools/fairywriter-rom` emits
the matching guest code.

There are three main data paths:

- a bounded command ring from cartridge to host;
- a bounded event/result ring from host to cartridge;
- two viewport snapshot slots from host to cartridge.

Every editing command carries the document revision it was based on. The host
accepts a command only against the expected revision, applies it to the
authoritative document, increments the revision, and publishes a new viewport.
That makes stale input explicit instead of allowing the two copies to drift.

The viewport slots are double-buffered. The host fills the inactive slot
completely, including its generation and integrity fields, then flips which
slot is active. The cartridge therefore sees either the old complete snapshot
or the new complete snapshot, never a half-written mixture.

## What a viewport contains

A viewport is a bounded window into the full document. It carries enough
information for the cartridge to render and navigate correctly:

- document revision and visible text start;
- caret and selection offsets;
- UTF-8 text bytes plus grapheme boundaries;
- explicit line-break/wrap boundaries;
- formatting and proofing runs;
- word, line, chapter, dirty, and read-only metadata;
- resident display title and document-position values.

The host remains authoritative for wrapping and grapheme movement because the
full Qt text model lives there. The cartridge uses the returned boundaries to
draw its 30x17 document grid and to make immediate local movement predictable.

When the caret approaches a viewport edge, the bridge publishes another
page-sized window. It does not recenter on every keystroke. That stable edge
guard avoids needless mailbox traffic and prevents the document from visually
jittering.

Every new document plane is cleared across all 510 visible cells before new
content is drawn. This is an important invariant: moving from a full page to a
short final page must not leave stale characters behind.

## Keyboard and mouse input

Desktop key events are translated into PS/2 Set 2 scan bytes. The runtime
exposes those bytes through an emulated XBAND keyboard on SNES controller port
2. The cartridge performs the keyboard transaction and owns the scan-code map,
modifier state, shortcuts, and editing command generation.

Pointer movement and buttons feed an emulated Super NES Mouse on controller
port 1. The cartridge reads the latched report and decides whether a gesture
places the caret, extends a selection, activates a toolbar control, or drags the
document-position thumb.

This is intentionally more work than sending native text directly to the
document engine. It keeps the real input path, timing, and interface behavior
inside the cartridge instead of presenting a cosmetic SNES shell around native
controls.

## Editing, files, and recovery

An edit normally follows this sequence:

1. the cartridge receives input and enqueues a revisioned command;
2. `DocumentBridge` drains the command ring;
3. `DocumentEngine` validates and applies the operation;
4. the host records the new authoritative state;
5. the bridge publishes a new viewport snapshot;
6. the cartridge consumes it and redraws the affected planes.

Save operations go through the host's format writers and atomic file-replacement
path. Recovery is also host-owned: closing with unsaved work synchronously
persists recovery state, and a later launch offers restoration through a
cartridge-owned prompt.

The file browser receives a bounded catalog of safe host-provided entries. It
never interprets raw directory memory or follows paths outside the host's
validated catalog boundary.

## Testing the boundary

FairyWriter avoids tests that only confirm a mock or a painted approximation.
The production gate includes:

- deterministic cartridge generation and machine-code contract tests;
- real 65816 execution through the SNES machine core;
- XBAND keyboard and Super NES Mouse protocol behavior;
- mailbox bounds, ordering, revision, and viewport integrity;
- real ODT import, editing, undo/redo, heading promotion, save, and reopen;
- recovery and file-catalog behavior;
- Linux AppImage X11 and Wayland launch checks.

The long ODT fixture is generated from openly publishable source during the
build. It is an independent standards-shaped ZIP/XML document, not one written
by FairyWriter itself, so it exercises the production reader without exposing a
private manuscript.

Automated tests still do not prove visual feel, physical mouse/keyboard input,
Windows first launch, or recovery on every target machine. Those manual
boundaries are recorded explicitly in `TESTING.md` and
`DEVELOPMENT_STATUS.md`.

## Why the constraints matter

The 256x224 screen, resident glyph set, bounded mailbox, and cartridge-owned UI
force deliberate decisions. They also make the architecture understandable:
small fixed data moves across one explicit boundary, the full document has one
owner, and each side can be tested against concrete invariants.

Future work should strengthen that model rather than bypass it with native
overlays, hidden fallback editors, unbounded shared state, or platform-specific
behavioral substitutes.
