# FairyWriter SNES Frontend

FairyWriter is a native app that presents a source-generated SNES cartridge runtime. It is not a desktop editor with an SNES skin.

## Core ownership model

- Cartridge runtime owns visible UI behavior: layout, cursor, selection, menus, dialogs, and rendered text state.
- Host (`DocumentEngine` + bridge) owns authoritative document state: files, revisions, undo, recovery, and statistics.
- The SRAM mailbox is the only authority boundary between guest and host.

## Frontend invariants

- The cartridge is source-generated and embedded; no commercial ROM assets are shipped.
- The displayed frame is a cartridge-produced 256x224 SNES framebuffer.
- Host UI layers must not redraw or restyle cartridge UI semantics.
- Keyboard input enters through the XBAND emulation path (controller port 2), not through direct desktop editor shortcuts.
- Invalid/partial mailbox records must not mutate host document state.

## Tester-facing host boundary

- Host logging is disabled unless `FAIRYWRITER_DEBUG_LOG` is set. The opt-in
  file lives under `QStandardPaths::TempLocation`; input must never cause
  unconditional per-event disk traffic.
- The host centers on the primary display and does not re-grab focus after
  deactivation. Platform window management belongs to the operating system.
- Close, New, Open, Recent, session switching, and Recovery share one
  cartridge-owned Checkpoint/Save/Discard/Cancel transition. The host accepts a
  transition only after the selected durable operation succeeds.
- The executable accepts zero or one initial document path. It maps keys into
  the XBAND scan path; Page Up, Page Down, F2, F3, and F4 are not host shortcuts.
- F2 enters cartridge mode `$0f`, a static 30x8 controls plane. `$0313` stores
  the exact origin mode. F1 or Backspace restores it, including the menu/browser
  state and menu selection; help is not a host-drawn dialog.
- F3 enters the cartridge-owned persistence settings plane. F4 enters the
  cartridge-owned Find plane.

## Production configuration and platform gate

- `FAIRYWRITER_DEVELOPMENT_UI=OFF` and
  `FAIRYWRITER_DEVELOPER_TOOLS=OFF` define the shipping build on every platform.
- Production regression expectation is 10/10. `fairywriter_persistence` also runs
  all 670 examples from the vendored GFM conformance corpus, and
  `fairywriter_persistence_process_e2e` launches fresh production processes for
  ODT, DOCX, RTF, Markdown, and crash recovery. Enabling the retired development
  presentation surface adds three tests, for 13/13.
- `fairywriter_cartridge_conformance` runs `tools/fairywriter-romcheck` against
  the built `.sfc`. It exists because the vendored emulator is lenient where real
  loaders are not: it never reads the map-mode byte (it infers LoROM vs HiROM
  from which candidate header location scores highest, `snes_other.c:169`) and it
  accepts any checksum pair summing to `$ffff`. The checker enforces the
  canonical checksum, map mode agreeing with the header's position, vectors
  resolving into the cartridge, and — the one that will matter as the ROM grows —
  that no rival header location can outscore the real one at `$7fc0`.
- Linux x86_64 targets Qt 6.8.3; Windows x64 targets Qt 6.8; macOS uses the
  installed arm64 Qt. Platform packaging may deploy libraries differently, but
  all three run the same cartridge/bridge/document implementation.
- Automated macOS, Linux-container, and Windows-runner evidence does not
  establish physical Windows first launch, macOS-Intel support, or packaged-app
  mouse/keyboard/dialog acceptance.

## Persistence ownership

- `DocumentEngine` owns a viewport revision and a separate content generation.
  Cursor movement, selection, search, and scrolling advance only presentation
  state; text and formatting changes advance content generation.
- `DocumentPersistence` owns typed load/save/save-as/checkpoint/autosave
  outcomes, full size/mtime/SHA-256 fingerprints, conflict decisions, and
  independent checksummed `.fwrecover` generations.
- The live `QTextDocument` stays on the UI thread. Persistence captures an
  immutable snapshot and submits every filesystem read, parse/encode, hash,
  sync, commit, session marker, and recovery rotation to one FIFO worker.
  Transitions synchronously wait for that worker's typed durable result.
- New documents and first Save default to ODT. Save As offers ODT, DOCX, RTF,
  and Markdown. Existing FODT and plain text remain compatibility formats.
- New-file writes stage beside the destination before a no-replace rename.
  Existing files use atomic replacement with direct-write fallback disabled.
- Timed persistence begins with the first content change after the last durable
  state. The defaults are Save + Recovery, one minute, and five retained
  generations; zero retained copies disables all timed persistence.
- Markdown source is authoritative UTF-8. The pinned cmark-gfm parser validates
  GFM while links, images, raw HTML, front matter, and destinations remain inert
  document data.

## Mailbox model (active)

- `commands` ring: cartridge -> host semantic operations.
- `events` ring: host -> cartridge responses/outcomes.
- `viewport` slots: host-committed snapshot windows with metadata.
- Viewport metadata includes grapheme offsets, line-break offsets, and format runs for cartridge layout/render use.

Every record uses versioned little-endian framing with protocol, kind, payload size, flags, sequence, and revision.

## Scope constraints (current)

- One visual identity and one resident font.
- Visible text is generated from the cartridge's resident glyph set. Host Qt font resources are not part of the SNES product or its platform packages.
- Rich/proofing target is intentionally limited:
  - bold / italic / underline
  - spelling issue styling
  - grammar issue styling
- No Unicode glyph streaming/emoji feature track.

## Proofing status

- Host side emits proofing format-run flags in viewport snapshots.
- Cartridge proofing visuals are reintroduced: `src/document_engine.cpp` draw-character handling uses a split path (plain document loop vs. proofing attribute overrides), and the ROM (`tools/fairywriter-rom/main.go`) consumes per-cell proof maps for spelling/grammar visuals.
- Realized palette (measured against a live build): the document surface is the same blue as the panel it sits in. Plain text is white on blue; spelling issues render the glyph in yellow on blue; grammar issues invert the cell to a solid white fill with a blue glyph. All three states remain clearly distinguishable, and bold/italic/underline stay shape-only so they compose with any of them.
- Current focus is monitoring proofing-enabled behavior during human app verification (real ODT fixture) for any remaining visual anomalies, not reintroduction.

## Document display and position status

- The document staging plane is 30x17 (510 cells), matching the 17-row VBlank upload. Every render clears the complete low-byte and attribute planes with a paired 16-bit loop before projecting text, so shorter viewports cannot inherit stale lower-row tiles.
- The bar above the document is now a real position track. Cartridge OAM sprite 1 renders a dedicated thumb at an X coordinate derived from committed `bytes_before / total_document_bytes`; the host supplies only document metadata and does not draw the indicator.
- OAM is persistent. The pointer is refreshed every VBlank for mouse motion, while the position thumb is written only when a new viewport marks its X coordinate dirty. Keep this event-driven split: rewriting both sprites every VBlank regressed input/render timing in the end-to-end machine test. A scroll drag is the one exception: it moves the thumb immediately so the drag feels attached to the pointer, and the next viewport republishes it from real byte offsets.

## Surface colour

Sub-palette 2 (selected by `documentBaseAttr`) is what every document, title and
toolbar cell renders through. It used to remap colour index 4 -- the blue the
static panels are already painted in -- to index 7, the maroon, so the cell
planes painted maroon over their own blue panels. Index 4 is left alone now and
the surfaces match. Static art that was explicitly maroon (the title card
interior and its speckle) moved into the same blue family; the card's amber
border stays as the manuscript accent.

## Panel geometry

The document panel's frame and the document plane are independent numbers in
different files, and they have drifted apart once already.

- The plane is 30x17 and DMAs to tilemap row 10, painting scene y 80..215.
- `scene()`'s panel must enclose that: `frame` insets its field by 5, so the
  field spans `y+5 .. y+h-6` and the panel needs height 170 at y=51.
- There is no footer bar: 17 rows plus a bottom border consume the screen.
- The BG renders one pixel above scene coordinates, so the document's first row
  lands at screen y=79. Measure a framebuffer column before concluding anything
  about vertical alignment here.
- `TestDocumentPanelEnclosesTheDocumentPlane` guards the pairing.

## Scrolling ownership

The position track is a draggable scrollbar, and it is the only thing that
decouples the published window from the cursor.

- The cartridge publishes `CommandScrollToFraction` (0x0110) carrying only the
  thumb's position along its travel. It never computes a document offset: it has
  no 32-bit divide, and document geometry is the host's.
- The host holds a scroll anchor that is authoritative for the next publish and
  is released by any command that moves the caret or edits. Scrolling therefore
  moves the view only; the caret is placed afterwards by clicking in the scrolled
  view, which is also what ends the scroll.
- Status flag bit 4 means the caret is not inside the published window. The
  cartridge must draw no caret at all in that case -- the viewport-relative cursor
  will match no drawn character, and the draw loop's end-of-text fallback would
  otherwise park a caret at the end of the visible text.
- A press on the track arms a scroll drag. A held drag follows that arming, not
  the pointer's current position, so leaving the 8-pixel track does not turn the
  drag into a text selection.

## Cartridge register-width invariant

`renderDocument` runs with **16-bit index registers** (`REP #$10` at entry,
`SEP #$30` at the VBlank handoff) and an 8-bit accumulator. Inside that region:

- `CPX #`/`CPY #`/`LDX #`/`LDY #` immediates must be emitted as **three** bytes.
  A two-byte immediate silently swallows the next opcode.
- `STX`/`STY`/`LDX`/`LDY` against a direct-page address claims **two**
  consecutive bytes. The caret cell is `$0b`/`$0c` and the word-start scratch is
  `$58`/`$59` for this reason.
- `TYA` transfers only the low byte, so any comparison built on it is limited to
  cells 0-255. The cell hit-test used to be built this way and now uses a 16-bit
  `CPY` instead.

Three separate defects across 2026-07-27 and 2026-07-28 came from overlooking
this. Treat it as a checklist item for every cartridge render edit.

Relatedly: never hand-compute a branch displacement in `tools/fairywriter-rom/main.go`.
The main loop closed with `b(0x80, byte(int8(...)))`, which silently wrapped once
the loop body grew past -128 and resumed execution mid-instruction. The
`branch()` helper panics on out-of-range; use it, or an absolute `JMP`.

## Cursor positioning ownership

Both the mouse pointer and wrap-aware `Up`/`Down` resolve a target cell against
the cartridge's real layout and publish an absolute caret position
(`CommandPointerSetCursor` / `CommandPointerExtendCursor`). The host adds the
viewport start and owns the authoritative caret; the cartridge never moves it
directly.

- `resolveCellCommand` is the single publish path. Callers set a 16-bit target
  cell and a command kind, and it re-renders with the hit-test raised.
- The hit-test takes the greatest output position not past the target, so a
  target beyond the end of a shorter line clamps onto that line.
- Per-character viewport-relative UTF-16 offsets live in `$0700` (low) and
  `$0900` (high), filled by the viewport decode. Nothing writes `$0400`.
- Vertical movement resolves against a caret snapshot taken before the local
  optimistic edit, because `editorDispatch` runs ahead of `commandEnqueue`.
- The pointer accepts all 17 document rows (screen y 80..215). Its row
  arithmetic, target cell, and drag repeat-suppression are all 16-bit, because
  `row*30` exceeds 255 from row 9 on and two of the 510 cells can share a low byte.
- The guest's own optimistic cursor is `$00`/`$01`, 16-bit. Nothing else may use
  `$01`; the pending key code lives at `$5b` for exactly this reason. `Up`/`Down`
  do not move it locally -- `verticalMove` sets it to the index it resolved, so
  the optimistic frame never contradicts what was published.

## Regression gates for frontend edits

After each ROM/frontend change:

1. `GO111MODULE=off go test ./tools/fairywriter-rom`
2. `ctest --test-dir build-arm64 -R fairywriter_xband_end_to_end --output-on-failure -VV`
3. `ctest --test-dir build-arm64 --output-on-failure`

Only sync the top-level app bundle after all gates pass.
