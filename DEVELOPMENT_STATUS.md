# FairyWriter Development Status (Synced 2026-07-29)

## 2026-07-29 persistence and cartridge reconciliation

The durable-persistence and SNES-conformance changes now share one public
`origin/main` base and one production gate. The cartridge is a canonical 64 KiB
two-bank LoROM with validated header selection, checksum, vectors, explicit DB
state, and bank-aware data access. `fairywriter_cartridge_conformance` raises
the production expectation to **10/10**.

Recovery generations now also carry an explicit resolved-transition bit. A
successful primary save or an explicit Discard retains older generations in
Recovery History but removes them from startup candidates; a later checkpoint
becomes the sole unresolved candidate. Version-1 recovery files remain readable
and conservatively unresolved.

## 2026-07-29 durable persistence, recovery, autosave, and Markdown

Persistence now has one production coordinator and one invariant: a transition
cannot replace or close a document until one immutable content generation has a
typed durable result.

### State and filesystem safety

- Viewport revisions are separate from content generations. Cursor movement,
  selection, Find, and scrolling never dirty a document or create recovery;
  text and formatting changes do.
- The UI thread captures immutable snapshots. A single FIFO worker performs
  file reads, parsing/encoding, SHA-256 hashing, recovery rotation, sync, and
  atomic commit. Close and document replacement wait for its result.
- New files use same-directory staging, complete write + sync, no-replace
  rename, and directory sync. Existing-file replacement disables direct-write
  fallback and checks size/mtime/SHA-256 before encoding and again immediately
  before commit. Failed or corrupt loads leave the active document untouched.
- ODT is the new-document and first-Save default. Save As offers ODT, DOCX, RTF,
  and Markdown; FODT and plain text remain compatibility formats.

### Autosave and recovery

- Defaults are Save + Recovery, one minute, and five retained copies. F3 exposes
  Recovery Only, the 1-255 minute interval, 0-255 copies, Recovery History, and
  Markdown Rendered/Source. Zero copies disables timed writes; explicit Save and
  pinned Checkpoint remain available.
- Independent checksummed `.fwrecover` generations retain document identity,
  original path/format, rich state or Markdown source, cursor/anchor, sequence,
  UTC time, content hash, and primary fingerprint. Adjacent duplicates collapse,
  A-B-A remains history, rotation happens only after commit, corrupt generations
  are skipped, and matching post-save history does not prompt at startup.
- Close, New, Open, Recent, session switching, and Recovery use one
  cartridge-owned Checkpoint/Save/Discard/Cancel transition. Restored recovery
  is dirty and retains its original path, format, and prior fingerprint.

### Markdown and validation

- Vendored cmark-gfm `0.29.0.gfm.13`
  (`587a12bb54d95ac37241377e6ddc93ea0e45439b`) provides reproducible offline
  GFM parsing. Source is authoritative UTF-8; untouched source is byte-stable,
  common rendered edits patch their smallest unique source span, and links,
  images, raw HTML, front matter, and destinations remain inert.
- Persistence adds two production tests. `fairywriter_persistence` runs the 670-example
  GFM corpus plus real filesystem, fingerprint, recovery, format, and Markdown
  regressions. `fairywriter_persistence_process_e2e` launches the actual
  production executable and drives ordinary cartridge input through
  ODT/DOCX/RTF/Markdown save/relaunch plus unclean checkpoint recovery.
- A macOS CI production job now accompanies Linux. Automated native macOS and
  Linux container evidence establishes persistence behavior; packaged
  mouse/keyboard/dialog layout still requires the short human smoke described
  in `TESTING.md`.
- Before the cartridge-conformance commit, the native macOS production gate and
  Qt 6.8.3 Linux container gate both passed **9/9**. That AppImage also passed
  its X11 and Wayland bounded launches; its SHA-256 is
  `878ce2e8d41d93a090b3a18f57ca0ac203bd765a546423042e469784aca3bdf4`.

## 2026-07-28 three-platform tester-readiness pass

The production source now has one explicit configuration across macOS, Linux,
and Windows. `FAIRYWRITER_DEVELOPMENT_UI` and
`FAIRYWRITER_DEVELOPER_TOOLS` both default off; the shipping target depends only
on Qt Core/Gui/Widgets/OpenGLWidgets, ZLIB, Go for cartridge generation, and the
pinned `snesrecomp` checkout. Production `ctest` is **7/7**. The three retired
Qt presentation/theme tests are registered only with development UI enabled,
where the expected count is still 10.

### Format and platform portability

- Iconv and all `Iconv::Iconv` links are removed. `TextCodec` supplies CP1252
  directly (Latin-1 plus the defined 0x80-0x9f punctuation range); unknown RTF
  codepages import through ISO-8859-1 instead of failing. Real RTF regressions
  cover curly punctuation and an unknown codepage.
- Windows configuration is enabled for MSVC x64. Durable writes use `_commit`,
  catalog containment uses normalized forward-slash paths with
  case-insensitive Windows comparison, and drive enumeration skips the volume
  already containing the home root.
- The production Windows executable is a GUI target with a multi-resolution
  icon. `packaging/windows/build-windows.ps1` configures with vcpkg, builds,
  runs the production gate, deploys Qt, audits the staging directory, and emits
  a ZIP plus SHA-256. It still requires execution and first-run acceptance on a
  real Windows host.

### Tester-facing runtime behavior

- `FAIRYWRITER_DEBUG_LOG` is checked once and logging is off by default; the
  opt-in log uses `QStandardPaths::TempLocation`.
- Window placement uses the primary screen. The Sidecar heuristic and the
  focus-out re-grab were removed.
- Startup/runtime failures show a named critical message and terminate cleanly.
  Closing the window synchronously writes recovery before acceptance.
- The host accepts one optional document path, exposes the same application
  identity on every platform, and maps F2, F3, Page Up, and Page Down through
  the real XBAND input route.
- F2 opens a cartridge-owned help plane. F1 or Backspace returns to the exact
  originating mode; returning to the menu also preserves the prior selection.
  Real-machine tests cover document and menu origins.

### Packaging and evidence

- `TESTING.md` is the tester contract: unsigned first-launch instructions,
  controls, file/recovery locations, real-ODT acceptance, debug-log reporting,
  and known issues.
- The Linux builder is now `packaging/linux/build-linux.sh` and targets official
  Qt 6.8.3 on an Ubuntu 22.04/glibc baseline. The macOS deploy script stages the
  pruned arm64 app, deploys Qt, ad-hoc signs it, audits it, and emits a DMG plus
  checksum. Both ship `COPYING` and `TESTING.md`.
- The former private long-document fixture was replaced for public development
  by a deterministic ODT generator. The production reader still exercises a
  substantial multi-chapter document, boundary edits, and save/reopen.
- Current macOS source validation: ROM generator green, focused XBAND green,
  full production `ctest` **7/7**, direct SNES help/ODT machine test green, and
  strict signing of the build bundle green.
- Fresh tester artifacts:
  - macOS arm64 DMG:
    `181279b216cd28a710394c2e7a1ebccee04b74d55c913b8c94c9035fa2e70a2c`.
    The mounted app contains only the Cocoa platform and native macOS style Qt
    plugins, reports 0.1.0, is arm64-only, passes strict deep signature
    verification, and contains no loose ROM/scratch data.
  - Linux x86_64 AppImage:
    `217ba2cd7a484a1c0bbadb03b7ab9377b88719fff9a4f370d5e92207967fcc0e`.
    The Qt 6.8.3/Ubuntu container passed 7/7, version, X11, Wayland, and
    ROM/scratch audits.
  - The top-level `FairyWriter.app` is synchronized byte-for-byte from the
    freshly green build executable and passes strict signing/version/arm64
    checks.

Not yet proved here: Windows compilation/runtime, macOS Intel, or the required
human mouse/keyboard/recovery acceptance pass on any platform. Do not promote
those items based on source inspection or container evidence.

## 2026-07-28 blue document surface

The maroon was one palette remap. Sub-palette 2 -- selected by
`documentBaseAttr`, and therefore used by every document, title and toolbar cell
-- remapped colour index 4 to index 7. Index 4 is the blue the static panels are
already painted in, so the cell planes were painting maroon over their own blue
panels. Removing the remap makes each surface match its panel.

- Static art that was explicitly maroon moved with it: the title card's interior
  was `c.rect(7, 7, 144, 37, 7)` and showed as a red band along the card's bottom
  edge (the live title cells only cover the top four rows). Interior and speckle
  are now in the blue family; the card's amber border stays as the accent.
- Proofing was verified by measuring cell colours in a framebuffer dump, not by
  eye, because spelling used a blue fill that only read against maroon. It still
  separates cleanly: plain is white on blue, spelling renders the glyph yellow,
  and grammar inverts the cell to a solid white fill with a blue glyph.
  Bold/italic/underline are shape-only and compose with all three.
- Sub-palettes 2 and 0 are now identical. Sub-palette 2 is kept as its own entry
  rather than repointing `documentBaseAttr`, since the attribute value is baked
  into the draw loop's priority tree and the SNES tests assert it directly.

## 2026-07-28 document panel encloses its plane

The maroon block hanging below the document's border was the document itself,
painting outside its frame.

- `scene()`'s document panel was `c.frame(4, 51, 248, 139, 4)`, sized back when
  the plane was 8 rows. The plane has been 30x17 for a while and DMAs to tilemap
  row 10, painting y 80..215 while the panel's field stopped at 185 -- so the
  document ran 26 lines past its own border and over the footer bar at y 194..219,
  burying its "F1 MENU / X SAVE / B BACK" hint.
- Panel height is now 170. `frame` insets its field by 5, so the field spans
  `y+5 .. y+h-6`; 170 puts the last field row exactly on the plane's last row.
- 17 rows leave no room for a footer once a bottom border is added, so the hint
  bar is removed rather than the text rows.
- Note for anyone measuring this area: the BG renders one pixel above scene
  coordinates, so the document's first row lands at screen y=79, not 80.
- `TestDocumentPanelEnclosesTheDocumentPlane` guards the pairing. Plane size lives
  in the render/DMA code and panel size lives in `scene()`; they are independent
  and had already drifted once without anyone noticing.

## 2026-07-28 draggable scrollbar

The document-position thumb is a real scrollbar now: press or drag anywhere on
the track to move through a long document. **It scrolls the view only** -- the
caret stays put and the user places it by clicking in the scrolled view. This is
the first thing in the codebase that decouples the published window from the
cursor.

- New wire command `CommandScrollToFraction` (0x0110). Payload is the thumb's
  position along its 226-pixel travel; the host decides what part of the document
  that is, since the cartridge has no 32-bit divide and the host owns document
  geometry.
- `DocumentEngine::makeViewport` gained `force_start`. `preferred_start` was only
  a stability hint before (honoured while it still contained the cursor, so the
  window would not re-centre every keystroke); `force_start` makes it
  authoritative so the window can leave the caret behind.
- `DocumentBridge::m_scroll_anchor` holds the scrolled position and is released by
  anything that moves the caret or edits, so clicking or typing snaps the view
  back. Clicking does not make the view jump, because the caret lands inside the
  scrolled window and that window stays the stability hint.
- Status flag bit 4 means "caret not in this window". Without it the cartridge
  computes a viewport-relative cursor matching no drawn character and the draw
  loop's end-of-text fallback parks a caret at the end of the visible text.
  The cartridge stores it in `$033e` and `drawCursor` skips the glyph.
- `$0333` records that a press landed on the track, so a held drag keeps
  scrolling even after the pointer leaves the thin 8-pixel strip instead of
  turning into a text selection. The thumb sprite follows the pointer immediately
  and `$0363` suppresses republishing an unchanged thumb every frame.
- Pressing anywhere on the track jumps there; requiring a hit on the 8-pixel thumb
  would be a poor target.

Two notes: dragging to the far end used to fail to publish, because anchoring at
the last character builds an empty window that trips `makeViewport`'s emptiness
check -- the forced path now backfills toward the start so the last screenful
stays full. And `beqMouseMenu` outgrew its 8-bit branch range once the track block
landed in the mouse region dispatch; it is now the standard trampoline.

## 2026-07-28 pointer reach and optimistic-frame accuracy

Both follow-ups from the vertical-movement work are closed; the second surfaced
a third defect in the same area.

- **Mouse clicks reach all 17 document rows.** The pointer rejected rows >= 8
  (`CMP #8`), stale from when the plane really was 8 rows. It has been 30x17 for
  a while — 17 DMA rows (`LDX #$11`) to tilemap row 10, screen y 80..215 — so the
  lower half of the document was silently unclickable. Bound raised to 17 and the
  row arithmetic widened to 16-bit (`row*30` exceeds 255 from row 9). The pointer
  column moved to `$0346`/`$0347` so the 16-bit add cannot run into the
  command-kind byte at `$0344`, and drag repeat-suppression compares 16-bit
  against `$0350`/`$0351`.
- **The local optimistic Up/Down move no longer contradicts what is published.**
  The handlers added or subtracted 30 *characters*, which is one screen row only
  on a layout that is exactly 30 characters wide. Since the local edit runs before
  `commandEnqueue` and its render waits for VBlank, the wrong caret really was
  displayed for a frame — measured at character 45 (clamped to end of document)
  against a published 23. The handlers no longer move the cursor; `verticalMove`
  sets it to the index it actually resolved.
- **The pending key code was overwriting the guest cursor's high byte.** `$00`/`$01`
  is the 16-bit guest cursor (16-bit `CPX $00` in the draw loop, 16-bit `INC $00`
  on insert), but the dispatch parked the key code in `$01`, so every keystroke
  made the cursor `position + keyCode*256` until the next viewport commit reset
  it. From character 102, Right left `$00`=103 with `$01`=18 — a cursor of 4711.
  Key code moved to `$5b`. Sixth instance of this register-width family.

Remaining polish noted, not measured: `left`/`right`/`home`/`end` still operate
on `$00` with 8-bit opcodes, so their optimistic frame is wrong for documents
over 255 characters in the same way Up/Down was.

## 2026-07-28 wrap-aware vertical cursor movement

`Up`/`Down` now follow the cartridge's own 30-column layout rather than the host
document's, with a classic sticky desired column. Host side unchanged:
`SetCursorPosition` (44) / `ExtendCursorPosition` (45) and the bridge's
viewport-start addition already existed for the mouse.

- A new `resolveCellCommand` subroutine is the one publish path shared by the
  pointer and vertical movement: given a 16-bit target cell (`$0339`/`$033a`)
  and a kind byte (`$0344`), it re-renders with the hit-test raised and publishes
  the resolved character's viewport-relative UTF-16 offset. The hit-test's
  "greatest output position not past the target" rule clamps onto shorter lines
  for free.
- The caret's column is captured during the render (`$035c`), so the target is
  pure addition -- `caretCell - caretColumn + stickyColumn -/+ 30` -- with no
  divide-by-30 in a path that already re-renders.
- `editorDispatch` runs the local optimistic edit *before* `commandEnqueue`, so
  the live caret registers describe where the local handler moved to, not where
  the user pressed the key. The main loop snapshots the caret cell and column
  into `$0365`/`$0367` before the local edit, and vertical movement resolves
  against the snapshot.
- Sticky column at `$033c`, staged into `$033d` at the top of the command
  dispatch and then cleared; only Up/Down write it back, so any other key ends a
  vertical run.
- `Up` from row 0, `Down` past the plane, and the `$01FE` "caret off screen"
  sentinel all fall back to the old semantic `MoveUp`/`MoveDown`, which is what
  still scrolls the viewport.

Four defects had to be fixed first, all of them blocking:

1. **Mouse clicks published offset 0.** The publish path read `$0400,X`, which
   nothing in the ROM ever writes, so the caret landed at the start of the
   viewport no matter where you clicked. The real per-character UTF-16 offset
   tables are `$0700` (low) / `$0900` (high). The high byte was also discarded
   (`STZ $1801`), capping addressable offsets at 255 even with the right base.
2. **The hit-test compare was 8-bit** (`TYA; CMP $0339` -- TYA moves only the low
   byte of a 16-bit `Y`), so resolution wrapped every 256 cells. Now `CPY $0339`.
3. **`STX $0341` is a 16-bit store owning `$0341` and `$0342`,** and the found
   flag sat at `$0342`, overwriting the character index's high byte on every hit.
   Flag moved to `$033b`.
4. **The main loop closed with a hand-computed `b(0x80, byte(int8(...)))`.**
   Adding the caret snapshot pushed it past -128; `int8()` wrapped silently and
   execution resumed mid-instruction, surfacing as an unrelated typing failure.
   Now an absolute `JMP`. Any remaining hand-computed displacement in this file
   is a latent instance of the same bug.

Cartridge static-data layout moved another `+0x100` (cumulative `+0x500`). In
the old 32 KiB layout, the later help-plane shift used all 554 available PPU
tile slots. The reconciled 64 KiB layout moves bulk data into bank 1 and has 889
cartridge slots before the rival-header reservation, while VRAM remains capped
at 1024 tiles.

Follow-ups this exposed (both since closed -- see the section above): the pointer
rejecting rows >= 8, and the local optimistic Up/Down move disagreeing with what
gets published.

## 2026-07-27 editing-correctness pass (word-fit, plane bound, caret collision)

Three defects fixed, each verified by building the ROM with and without the fix
and confirming the new regression test fails then passes.

- **Word-fit 8-bit addition overflow — the item previously flagged as "not yet
  fixed, needs its own fix before being considered closed" is now closed.** The
  fit check summed `column + wordLen` in an 8-bit accumulator; the word counter
  saturates to 31 only when its own `INC` wraps 255->0, so it legally reaches
  255 and the sum could wrap on its own. A 240-character unbroken word at
  column 16 summed to exactly 256, compared below 31, and drew mid-line. Fixed
  by rejecting any word of 31+ cells before the addition, which cannot fit at
  any nonzero column anyway. The new comparison runs once per word, staying out
  of the per-character measure loop that is documented as timing-sensitive.
- **Dead screen-full bound in `advanceToLine` (new discovery).** `renderDocument`
  runs under `REP #$10`, so index registers are 16-bit and `CPY #imm` takes three
  bytes — but the bound was emitted as the two-byte `c0 f0`. It swallowed the
  following `BCC` opcode, decoded as `CPY #$90F0`, and reduced the intended
  `BCC screenNotFull / JMP screenFull` guard to two garbage instructions
  (`ORA $4c,S`, `AND [$9f],Y`) that only clobbered A, which the next `LDA $0a`
  reloaded — so the loop still terminated on column 30 and nothing looked wrong.
  With no live bound, word-wrap padding could carry the output cursor up to 29
  cells past the 510-cell plane and the following draw wrote out of bounds
  (confirmed: a glyph landed at `$11FE`, one past the plane; the worst case
  reaches `$1216`, inside the attribute plane). Now a correct three-byte
  `CPY #$01FE`, matching the bound `drawLoop` already uses instead of the stale
  240 (8-row) value from before the plane grew to 30x17.
- **Caret cell / word-counter zero-page collision — the third collision of this
  shape in this file, after `$19`/`$1b` and `$09`.** With 16-bit index registers,
  `positionCursor`'s `STY $0b` writes a 16-bit cell across `$0b` *and* `$0c`,
  and `drawCursor` reads it back with a 16-bit `LDY $0b`. The word-length counter
  also lived at `$0c`, so every word measured after the caret was captured
  destroyed the caret's high byte, sending the glyph to
  `$1000 + (lastWordLen<<8) + column` — outside the document plane. The result:
  **no caret was drawn at all unless the cursor sat at the natural end of text**,
  the only path that captures `$0b`/`$0c` immediately before `drawCursor`.
  Confirmed with cursor 8 in a 61-character document putting the caret at `$1608`
  instead of `$1008` (`$0b=$08` correct, `$0c=$06` left from measuring "juliet").
  Fixed by moving the word-length counter to `$5a`.
- **The open 2026-07-26 shift+arrow caret observation is resolved by the above,
  not a separate bug.** The host was always correct: `makeViewport` publishes
  `m_cursor.position()`, which for a selection is the active edge. Shift+arrow
  just moves the cursor off the natural end of text, precisely the case that lost
  the caret. Permanent coverage now asserts the caret sits on the active edge of
  an in-progress selection.
- ~~Not done this session: wrap-aware `Up`/`Down` cursor movement.~~ Landed on
  2026-07-28; see the section above.

## Earlier 2026-07-27 status
Authoritative docs:
- `docs/HANDOFF_2026-07-26.md` (execution handoff)
- `plan.md` (forward plan)
- `docs/SNES_FRONTEND.md` (frontend ownership model and constraints)
## Current state
- Proofing visuals are enabled with a split ROM draw path that isolates plain document rendering from proofing attribute overrides.
- Text-selection highlighting is reintroduced (regressed silently in the proofing-stabilization commit) with a non-colliding attribute encoding, verified both by SNES-level tests and live in the app.
- A generated public ODT fixture is wired into automated `ctest` (the earlier
  real-document path was gated behind a manual CLI argument that was never
  passed).
- Latest full regression status: `ctest --test-dir build-arm64 --output-on-failure` = **10/10 passed**.
- Top-level app bundle has been refreshed from the validated build after the document-plane clear and position-thumb work. The build and top-level bundles are arm64, ad-hoc signed as `io.github.navjack.FairyWriter`, and pass strict deep signature verification.
## Implemented and active
- Expanded viewport metadata path is active in host serialization.
- Host emits format-run metadata including proofing flags.
- Selection normalization in viewport snapshots was added for visual coherence (no-selection case normalizes to cursor).
- New test coverage exists for proofing metadata emission in document engine tests.
- SNES machine tests now assert deterministic document-cell attributes and caret glyph placement/movement invariants.
- ROM source now defines explicit document/proofing attribute constants and consumes per-cell proof maps for spelling/grammar visuals.
- Document draw-character handling now uses an explicit split path so the baseline typing/render loop remains plain when proofing overrides are inactive.
- Selection-range highlighting is restored in the ROM draw loop: selection bounds are tracked in dedicated zero-page registers (`$50` start-inclusive/`$52` end-exclusive) rather than the previously-used `$19`/`$1b`, which turned out to be reused as scratch by both the draw loop's own word-wrap measurement code and title-bar rendering within the same render pass -- a real pre-existing collision, not just a proofing-commit regression. Selection reuses the spelling-issue visual style and takes priority over proofing when both apply to the same cell (documented and tested).
- `tools/public-test-fixture` generates a deterministic standards-shaped ODT
  whose filename, text, and three chapter markers are exercised automatically
  via `ctest`; no private manuscript is part of the repository.
- Bold/italic/underline rendering is implemented end to end. The host already transmitted these format-run flags; the ROM previously masked them out (`AND #$18`) before they reached the per-cell style map. Fixed by widening the mask to `#$1f` at both the projection loop and the draw loop, and adding three new 128-tile glyph-shape pages (underline/bold/italic, ids 128-511) generated by pixel-transforming the existing resident font (underline: lit 8th row; bold: 1px right dilation; italic: up-to-2px top-weighted right skew). Style (shape) and proofing (palette) are independent bit fields in the SNES tilemap attribute byte and combine correctly on the same cell (e.g. bold+spelling), confirmed both by SNES-level tests and a direct framebuffer/PPM visual dump. Priority when more than one style bit is set on the same run: underline > bold > italic. Selection continues to take priority over both proofing and style. Gated by a real `richStyleVisualsEnabled` switch (unlike the pre-existing `proofingVisualsEnabled`, which was found to be inert/never referenced).
- The ROM's static-data layout (`menuOffset` and everything after it) moved by a cumulative `+0x400` in this historical pass: rich-style rendering, toolbar staging/DMA, and the document-position path each consumed explicit program-space pages. The later help-plane shift and current rebuild use 554 PPU tiles, which exactly fill the gap before `scanMapOffset` (17728 bytes = 554 * 32) -- slot 555 panics the build. An earlier revision of this line claimed 76 spare slots; that has not been true since the help plane landed. Two styled-path branches use the standard 65816 long-branch trampoline (inverted condition skipping over an absolute `JMP`).
## 2026-07-27 caret positioning, Page Up/Down, and word-wrap overflow fixes
- Caret positioning bug fixed: the draw loop had two unconditional `$0b` overwrite sites (natural end-of-text, and the `screenFull` overflow sentinel) that clobbered the one true per-character caret capture whenever the cursor sat before the last rendered character. Both are now guarded by a new "already found" flag (`$56`), zeroed once per render pass. A separate, previously-masked bug was exposed by this fix: `drawCursor` unconditionally rewrote the cell's attribute byte (not just the glyph tile) wherever it drew the caret, which used to be harmless only because the caret was always misplaced past the end of real text; fixed by dropping the attribute write.
- Page Up/Page Down now emit `MovePageUp`/`MovePageDown` commands from the document editor: `command_enqueue`'s dispatch chain previously stopped checking at `End` (0x17) and silently dropped both codes as non-printable, even though the host already implemented both commands and the file browser's separate dispatcher already handled them.
- Word-wrap counter overflow fixed: `$0c` (word-length counter) is incremented via 8-bit `INC` in the per-character measure loop; a single unbroken run of 256+ non-space characters used to wrap it back toward 0, which could make an oversized word falsely appear to fit on the current line. Fixed cheaply (one extra `BNE` on the common no-wrap path, to avoid regressing the measure loop's already-tight per-frame cycle budget): when `INC` wraps 255->0, the counter is reset to 31, since any count >=31 already means "doesn't fit" regardless of true length.
- Found and fixed a separate, more significant bug while verifying the above: **`$09` was double-used** as both the high byte of the 16-bit decoded document length (`$08`/`$09`, persisted once by the viewport decode routine) and the draw loop's per-word "word-start X" scratch (`STX $09` in `measureWord`, which runs on every redraw pass). Any document over 255 characters had its length silently truncated to `length % 256` as soon as the first word was measured, corrupting rendering for the entire rest of the document. Fixed by moving the word-start scratch to `$58` (confirmed free), leaving `$08`/`$09` untouched. Verified with a standalone harness sweeping document lengths 10 through 300 before and after the fix.
- ~~Not yet fixed, narrower and out of scope for this pass: the word-fit check itself (`column + wordLen >= 31`) adds `$0c` to the column using an 8-bit accumulator~~ — **closed** in the later 2026-07-27 pass documented at the top of this file. Note the word-length counter has since moved from `$0c` to `$5a`; `$0c` is now the caret cell's high byte.
- All three fixes verified via the full gate sequence (Go tests, xband end-to-end, full `ctest` = 10/10) plus a new permanent SNES-level regression test for the word-wrap overflow (`tests/snes_machine_tests.cpp`). Live-app visual checks done for the caret and Page Up/Down fixes.

## 2026-07-27 document-plane clear and position indicator
- Confirmed the clear-bound defect empirically before fixing it: after committing 300 characters and then a two-character viewport, staged cell 250 retained stale tile `0x78` because `clearStage` stopped at 240 with an 8-bit `CPX #$F0`.
- `clearStage` now resets the complete 30x17/510-cell low-byte and attribute planes. It uses 16-bit A/X/Y locally and writes two cells per iteration (`$2020` glyphs, `$0808` attributes), then restores 8-bit widths before the smaller title/status/menu loops. A first 510-iteration byte clear was discarded because it caused real end-to-end key loss; the paired 255-iteration version preserves throughput and passes repeat stress.
- A real position thumb now rides the static 234px track. Confirmed sprite 1 is free, added one dedicated 8x8 OBJ tile, and map committed 32-bit `bytes_before / total_document_bytes` metadata to track X through common right-shift normalization plus the SNES hardware multiply/divide registers.
- Thumb position and dirty state live at `$0352/$0353`. OAM sprite 1 is updated only when new viewport metadata marks it dirty; an initial always-rewrite implementation regressed the existing word-wrap/input test because it added unnecessary work to every VBlank. The mouse pointer remains continuously refreshed.
- Permanent SNES-machine coverage commits long then short viewports and verifies cell 250 returns to tile/attribute `0x20/0x08`. A second test commits a 1001-byte logical document at start/middle/end and verifies thumb X `11/124/237` plus visible framebuffer movement.
- Validation is green: Go ROM tests pass, focused XBAND passes, full `ctest` is 10/10, and XBAND passed 10 consecutive repeat runs.
- Removed the obsolete personal-font packaging path. The SNES product renders its source-generated resident cartridge glyphs and never consumed the embedded Yoshi/Filgaia Qt resources, so the old build option, resource registration, separate Linux artifact, and font notice payload were dead product state. Production finalization now removes any stale `font-notices` directory before ad-hoc signing the complete macOS bundle.
- The containerized Linux gate exposed a platform-dependent flaw in the real-ODT regression: it tried to edit the supplied fixture directly, but `/src` is intentionally mounted read-only, so `DocumentEngine` correctly opened it read-only. The test now copies the actual ODT into a writable temporary workspace before exercising heading promotion, boundary edits, undo/redo, and save/reload.
- macOS and containerized Linux suites both pass 10/10. The canonical `FairyWriter-0.1.0-x86_64.AppImage` also passed `--version`, X11 and Wayland bounded launch checks, ROM/scratch exclusion, and the private-font resource leak audit.
- Repository-root cleanup removed superseded build trees, app snapshots, loose
  cartridge/save files, stale release archives, and private inputs from the
  publishable tree. `SCRATCH.MD` was restored and promoted out of ignored-output
  status after its durable working-memory role was clarified: future agents
  must keep its current resume point updated rather than prune it. Ignored local
  creative source material and packaged artifacts remain outside Git.

## In progress
- Manual app verification pass (2026-07-26) confirmed proofing visuals render and are distinguishable:
  - Spelling-flagged words (common-typo heuristic) render as a solid filled cell with accent-colored glyph.
  - Grammar-flagged repeated words render as a bordered/outlined glyph on the normal background.
  - Grammar-flagged double-space gaps render as a solid filled cell (no glyph).
  - No freezes/hangs observed typing or during proofing re-render.
- Docs wording for the proofing palette was corrected to match the implementation (blue/orange/cream, not literal red/green) in `plan.md` and `docs/SNES_FRONTEND.md`.
- ~~Observed but out of scope for this pass: while restoring selection highlighting, the blinking caret glyph appeared to stay at the document end rather than tracking the active edge of an in-progress shift+arrow selection.~~ — **confirmed and fixed** in the later 2026-07-27 pass. Root cause was the `$0b`/`$0c` caret/word-counter collision, not the selection code: the caret was lost for *any* cursor away from the natural end of text, and shift+arrow is simply the common way to get there.
- User has proposed a mouse-driven toolbar for Bold/Italic/Underline plus left/center/right justify, and floated bulleted/numbered lists too. Alignment already has host-side plumbing (`align_val` in `document_engine.cpp`'s format-run extraction) but no ROM rendering or UI; lists would be new scope on both host and cartridge. Needs its own scoping pass (mouse click-target layout, new host commands, and a check against the "WordPerfect for DOS simplicity" scope constraint) before implementation.
- Manual verification is still required for the new lower-row clear and position thumb on the real ODT fixture; automated tests prove staging/OAM/framebuffer behavior but not live visual feel.
- Next confirmed work item: the manual acceptance pass on the real ODT fixture (see `SCRATCH.MD` next actions). Conclusive live toolbar verification is the follow-up after that.
## Key risk controls
- Keep ROM changes localized to rendering where possible.
- Avoid altering fragile viewport decode/control-flow paths unless unavoidable.
- Require green ROM tests + xband end-to-end + full ctest before app sync.
- When reusing a zero-page address across ROM routines, verify empirically (via `fairy_snes_debug_wram` in a throwaway diagnostic, not just static reading) that no other routine in the same commit/render pass also claims it -- this codebase has **three** such collisions on record (`$19`/`$1b`, `$09`, and `$0c`).
- `renderDocument` runs with **16-bit index registers** (`REP #$10` at entry, `SEP #$30` at the VBlank handoff) and an 8-bit accumulator. Inside that region every `CPX #`/`CPY #`/`LDX #`/`LDY #` immediate must be emitted as three bytes, and every `STX`/`STY`/`LDX`/`LDY` to a direct-page address claims **two** consecutive bytes. Two of the three fixes in the later 2026-07-27 pass were caused by overlooking exactly this.
- Do not trust CMake to regenerate the cartridge from `main.go` on this volume; `touch` the source first and confirm the intended bytes are present in `build-arm64/fairywriter.sfc` before believing a test result. Building only the `fairywriter_cartridge` target leaves the embedded copy stale and trips `fairywriter_cartridge_contract` (exit 54) -- build `fairywriter_snes_machine_tests` to refresh both.
- Keep every `snes_machine_tests.cpp` return code below 256; the process exit status is the low 8 bits of `main`'s return, so `return 256` silently reports success.
## Out of scope
- Unicode glyph streaming/emoji pipeline.
- Broad feature expansion beyond the constrained one-font proofing/rich-style target.
