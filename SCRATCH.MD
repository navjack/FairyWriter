# FairyWriter Implementation Scratchpad

> **Durable working-memory contract.** This file is FairyWriter's external
> working brain. It exists specifically to survive agent context compaction and
> handoffs. Every agent continuing development must read it before editing and
> update the current sections after meaningful discoveries, decisions,
> validation, artifact changes, or changes to the next action. It is not a
> generated file, not disposable scratch output, and must never be pruned merely
> because some entries are historical. Move superseded detail into the archive
> instead of deleting the file.

## Current resume point — 2026-07-28

### Product and ownership invariants

- The active product is a native app running an embedded, source-generated SNES
  cartridge. It is not the legacy Qt editor with an SNES skin.
- The cartridge owns the visible 256x224 UI, resident glyphs, layout, caret,
  selection, menus, and status/position rendering.
- `DocumentEngine` and the host bridge own authoritative document/file state,
  undo/redo, recovery, statistics, and viewport metadata.
- The SRAM mailbox is the authority boundary. Keyboard input reaches the
  cartridge through the XBAND controller-port-2 path.
- Production has one visual identity and one resident cartridge glyph set.
  Yoshi/Filgaia Qt font embedding and the associated notice/artifact variants
  have been removed.

### Read order

1. `SCRATCH.MD` — live working state, recent discoveries, and failure history.
2. `plan.md` — product scope and ordered forward plan.
3. `DEVELOPMENT_STATUS.md` — implemented behavior and open work.
4. `docs/HANDOFF_2026-07-26.md` — exact resume and validation protocol.
5. `docs/SNES_FRONTEND.md` — guest/host ownership and frontend invariants.

### Active working tree

- Branch: `landmark1-viewport-wireformat`.
- The local branch retains the full private development history. That history
  contains old packaged applications and must not be pushed. Public GitHub
  `main` will be created as a new root commit from the sanitized source tree,
  leaving all local history and branches intact.
- Local captures, documents, packaged applications, and the ignored
  `FairyWriter/` artifact directory remain untracked and untouched.
- The former private ODT fixture has been removed from the publishable source.
  `tools/public-test-fixture` now generates a deterministic substantial ODT for
  the same production import/edit/round-trip test.
- A pinned `snesrecomp` bootstrap plus checked-in XBAND keyboard/Super NES
  Mouse patch replaces the previous dependency on a neighboring dirty checkout.
- Public-facing README, architecture/build/contribution/security/license
  indexes, GitHub templates, and Linux CI are in progress in this working tree.
- `build-arm64` is the one active native build directory.
- `build-arm64/FairyWriter.app` is the freshly built production app. The
  top-level `FairyWriter.app` has been synchronized from it after the complete
  green macOS artifact gate; both pass strict signature/version/arm64 checks.
- `release-linux-x86_64/FairyWriter-0.1.0-x86_64.AppImage` is the canonical
  Linux artifact and is fresh from the complete Qt 6.8.3 container gate.
- `ideas/` remains ignored personal reference artwork. It was deliberately kept
  during repository cleanup because it is creative source material.

### Completed in the latest pass (three-platform tester readiness)

Invariant: the production configuration is the same cartridge, bridge, document
engine, and seven real tests on macOS arm64, Linux x86_64, and Windows x64.
Platform packaging may differ; product behavior and ownership may not.

- Iconv is deleted from the production graph. A direct CP1252 implementation
  covers the visible punctuation range and unknown RTF codepages fall back to
  Latin-1. Real-RTF tests cover both paths.
- Tester-facing host behavior is cleaned up: logging is opt-in and checks its
  environment variable once; the Sidecar target and focus re-grab are gone;
  failures are named; close persists recovery synchronously; application
  identity is explicit; one initial document path is supported; and F2/F3/Page
  Up/Page Down enter the real XBAND path.
- F2 help is cartridge-owned mode `$0f`. `$0313` records the origin mode, so F1
  or Backspace returns to the document/menu/browser state without flattening
  state or losing menu selection. Machine tests cover both origins and both
  return keys.
- Windows source/configuration and its ZIP builder now exist. File durability,
  normalized/case-insensitive catalog containment, root enumeration, GUI target
  metadata, and executable icon are portable. This is source evidence only
  until a Windows x64 host builds and runs it.
- Production registers 7 tests. The three retired presentation/theme tests are
  available only with development UI enabled, for 10 total.
- Linux packaging is now `packaging/linux/build-linux.sh` and uses official Qt
  6.8.3 binaries on the Ubuntu 22.04/glibc baseline. macOS packaging stages the
  pruned arm64 bundle, deploys Qt, ad-hoc signs it, audits it, and makes a DMG.
  Windows packaging targets Qt 6.8/vcpkg/windeployqt. Every artifact ships
  `COPYING` and `TESTING.md`.
- The former long-document ODT fixture had once been restored after accidental
  framebuffer overwrite. Before public development it was removed because it
  contained private writing; the generated public ODT now preserves the real
  reader/edit/save/reopen coverage without publishing that content.

Current automated evidence: macOS ROM generator green, focused XBAND green,
production `ctest` **7/7**, direct SNES help/ODT coverage green, build bundle
strict signing green, and a mounted/audited arm64 DMG green. Linux Qt 6.8.3
container production `ctest` **7/7**, version, X11, Wayland, and ROM/scratch
checks are green. Artifact hashes:

- macOS DMG:
  `181279b216cd28a710394c2e7a1ebccee04b74d55c913b8c94c9035fa2e70a2c`
- Linux AppImage:
  `217ba2cd7a484a1c0bbadb03b7ab9377b88719fff9a4f370d5e92207967fcc0e`

Still manual/unproved: Windows build and first run, macOS Intel, and the full
real-fixture mouse/keyboard/recovery pass on each platform. Do not launch the
input-capturing GUI unattended to close that gap.

Next concrete action: validate the dependency bootstrap from a fresh directory,
run the complete macOS production gate and secret/content audits, create a clean
public root commit, and push it as `navjack/FairyWriter` `main`. After
publication, run the Windows builder and the human acceptance checklist on
actual target systems.

### Completed in the latest pass (blue document surface)

The maroon was a single palette remap, not scattered art. Sub-palette 2 --
selected by `documentBaseAttr`, so used by every document, title and toolbar
cell -- remapped colour index 4 to index 7. Index 4 is the blue the static
panels are *already* painted in, so the cell planes were painting maroon over
their own blue panels. Removing the remap makes every surface match its panel.

- The title card's interior was separately hard-coded maroon in `scene()`
  (`c.rect(7, 7, 144, 37, 7)`). The live title cells only cover the card's top
  four rows, so once the cell planes went blue it showed as a red band along the
  card's bottom edge. Interior and speckle are now blue-family; the amber border
  stays as the manuscript accent.
- **Proofing had to be re-verified by measurement, not by eye.** Spelling used a
  blue *fill*, which only read as a highlight against maroon -- making the
  document blue could have erased it silently. Measured from a framebuffer dump:
  plain is white on blue, spelling renders its glyph yellow, grammar inverts the
  cell to a solid white fill with a blue glyph. All three still separate cleanly,
  and bold/italic/underline are shape-only so they compose with any of them.
- A first attempt to check this proved nothing: the test helper `commitViewport`
  zeroes the format-run count, so runs written before it are wiped. Write the
  runs *after* the commit and bump the generation again.
- Sub-palettes 2 and 0 are now identical. Sub-palette 2 is kept rather than
  repointing `documentBaseAttr`, because that attribute value is baked into the
  draw loop's priority tree and asserted directly by the SNES tests.

### Completed in the previous pass (document panel encloses its plane)

The stray maroon block hanging below the document border was not a stray tile at
all — it was the document itself, painting outside its own frame.

- `scene()`'s document panel was `c.frame(4, 51, 248, 139, 4)`, sized when the
  document plane was 8 rows. The plane has been 30x17 for a while and DMAs to
  tilemap row 10, so it paints y 80..215 while the panel's field stopped at 185.
  The document ran 26 lines past its border and straight over the footer bar at
  y 194..219, burying its "F1 MENU / X SAVE / B BACK" hint under maroon cells.
- Panel height is now 170: `frame` insets its field by 5, so the field runs
  `y+5 .. y+h-6` and 170 puts the last field row exactly on the plane's last row.
- 17 rows leave no room for a footer once a bottom border is added, so **the hint
  bar is gone** rather than the text rows. That was the user's explicit choice
  over clipping the document to 13 rows or compromising at 15.
- **The BG sits one pixel above scene coordinates.** Scene y=51 renders at screen
  y=50 and the document's first row lands at screen y=79, not 80. This cost real
  time to work out: measured colour bands looked like the border was clipping the
  document's last row when it was not. Measure a framebuffer column before
  concluding anything about vertical alignment here.
- `TestDocumentPanelEnclosesTheDocumentPlane` now guards this. The plane's size
  lives in the DMA/render code and the panel's size lives in `scene()`; they are
  independent numbers and they had already drifted apart once silently. The test
  fails with the exact row where the panel stops.

### Completed in the previous pass (draggable scrollbar)

The document-position thumb is now a real scrollbar: press or drag anywhere on
the track to scroll a long document. **It scrolls the view only — the caret stays
where it is, and the user places it by clicking in the scrolled view.** That was
a deliberate product choice over the cheaper "drag moves the caret" option, and
it is the first thing in this codebase that decouples the window from the cursor.

- **New wire command `CommandScrollToFraction` (0x0110).** Payload is the thumb's
  position along its 226-pixel travel as a 16-bit numerator; the host decides what
  part of the document that is. The cartridge deliberately does not compute a
  document offset — it has no 32-bit divide and the host already owns document
  geometry.
- **Host: `makeViewport` gained a `force_start` parameter.** `preferred_start` was
  previously only a stability hint, honoured while it still contained the cursor
  so the window would not re-centre on every keystroke. `force_start` makes it
  authoritative, which is what lets the window move off the caret entirely.
- **`DocumentBridge::m_scroll_anchor`** holds the scrolled position. It is
  authoritative for the next publish and is released by anything that moves the
  caret or edits — clicking, typing, arrows — so the view snaps back to the caret.
  Clicking does not make the view jump, because the caret is placed *inside* the
  scrolled window and that window remains the stability hint.
- **Status flag bit 4 = "caret not in this window".** Without it the cartridge
  computes a viewport-relative cursor that matches no drawn character, and the
  draw loop's end-of-text fallback parks a caret at the end of the visible text —
  which would look like the caret had jumped there. The cartridge reads the bit
  into `$033e` and `drawCursor` skips the glyph entirely.
- **Cartridge: `$0333` records that a press landed on the track.** A held drag
  consults it rather than the pointer's current position, so dragging vertically
  off the thin 8-pixel track keeps scrolling instead of turning into a text
  selection. Cleared on release and on any document-body press.
- The thumb sprite moves immediately on drag rather than waiting for the host, so
  the drag feels attached to the pointer; the next viewport republishes it from
  real byte offsets anyway. `$0363` holds the last published thumb so a held
  button does not flood the command ring with an identical scroll every frame.
- Pressing anywhere on the track jumps there — the 8-pixel thumb would be a poor
  target to require hitting.

Two things worth knowing:

- **Dragging to the far end used to fail to publish.** Anchoring at the very last
  character builds an empty window, which trips `makeViewport`'s emptiness check
  and freezes the view. The forced path now backfills toward the start so the last
  screenful stays full, mirroring what the cursor-anchored path already did.
- `beqMouseMenu` outgrew its 8-bit branch range once the track block landed in the
  mouse region dispatch, and is now the standard inverted-BNE-over-JMP trampoline.
  Expect this every time that dispatch grows.

### Completed in the previous pass (pointer reach and optimistic-frame accuracy)

Both follow-ups from the vertical-movement pass are closed, and closing the
second one surfaced a third defect in the same area.

1. **Mouse clicks now reach all 17 document rows.** The pointer rejected rows
   >= 8 (`CMP #8`), a leftover from when the document plane really was 8 rows.
   It has been 30x17 for a while — it DMAs 17 rows (`LDX #$11`) to tilemap row
   10, i.e. screen y 80..215 — so the entire lower half of the document was
   silently unclickable. The bound is now 17, and the row arithmetic went 16-bit
   with it: `row*30` exceeds 255 from row 9 on. The pointer column moved to
   `$0346`/`$0347` so the 16-bit add cannot run into the command-kind byte at
   `$0344`, and the drag repeat-suppression compare is 16-bit against
   `$0350`/`$0351` (with 510 reachable cells, two different cells can share a low
   byte).

2. **The local optimistic Up/Down move no longer contradicts what is published.**
   `editorDispatch`'s handlers added or subtracted 30 *characters*, which equals
   one screen row only when the layout is exactly 30 characters wide — no word
   wrap, no CRs. Because that local edit runs before `commandEnqueue` and its
   render waits for VBlank, the wrong caret really was displayed for a frame.
   Measured on the ragged three-line fixture: the local move clamped the cursor
   to character 45 (end of document) while the wrap-aware resolution published
   character 23. The handlers no longer move the cursor at all; `verticalMove`
   sets it to the character index it actually resolved, so the optimistic frame
   agrees with the published position instead of guessing.

3. **The pending key code was overwriting the guest cursor's high byte** — found
   while verifying (2), and the sixth instance of this register-width family.
   `$00`/`$01` is the 16-bit guest cursor (the draw loop matches the caret with a
   16-bit `CPX $00`; the insert path advances it with a 16-bit `INC $00`), but
   the dispatch parked the key code in `$01`. Every keystroke turned the cursor
   into `position + keyCode*256` until the host's next viewport commit reset it.
   Measured: from character 102, pressing Right left `$00`=103 with `$01`=18 —
   a cursor of 4711, putting the optimistic caret far outside the document. The
   key code now lives at `$5b`.

### Completed in the previous pass (wrap-aware vertical movement)

`Up`/`Down` now follow the cartridge's own 30-column layout instead of the host
document's, with a classic sticky desired column. Implemented in
`tools/fairywriter-rom/main.go`; the host needed no changes at all, because
`SetCursorPosition` (44) / `ExtendCursorPosition` (45) and the bridge's
viewport-start addition already existed for the mouse.

- **Shape.** A new `resolveCellCommand` subroutine is the single publish path
  shared by the pointer and by vertical movement: given a 16-bit target cell in
  `$0339`/`$033a` and a kind byte in `$0344`, it re-renders with the hit-test
  raised and publishes the resolved character's viewport-relative UTF-16 offset.
  `verticalMove` only has to decide *which* cell it wants. The hit-test's
  "greatest output position not past the target" rule gives clamping onto a
  shorter line for free.
- **No division by 30.** The caret's column is now captured alongside its cell
  during the render (`$035c`), so the target is pure addition:
  `caretCell - caretColumn + stickyColumn -/+ 30`.
- **Order of operations was the real trap.** `editorDispatch` runs the local
  optimistic edit *before* `commandEnqueue`, so by the time `verticalMove` runs,
  the live caret registers already describe where the local handler moved to,
  not where the user pressed the key. The main loop now snapshots the caret cell
  and column into `$0365`/`$0367` before the local edit, and vertical movement
  resolves against that snapshot.
- **Sticky column** lives at `$033c`, staged into `$033d` at the top of the
  command dispatch and then cleared. Only `Up`/`Down` write it back, so any other
  key ends a vertical run. Staging first is what lets a run of vertical moves
  still read the previous value — clearing in place would wipe it on the very
  keypress that needs it.
- **Edge behavior:** `Up` from row 0 and `Down` past the plane fall back to the
  old semantic `MoveUp`/`MoveDown`, which is what still scrolls the viewport.
  The same fallback covers the `$01FE` "caret not on screen" sentinel.

Four defects had to be fixed first; each was blocking:

1. **The pointer's publish read `$0400,X`, which nothing in the ROM ever
   writes.** Every mouse click therefore published offset 0 and dropped the caret
   at the start of the viewport regardless of where you clicked. The tables the
   viewport decode actually fills are `$0700` (low) / `$0900` (high), indexed by
   document character index. The high byte was also being discarded
   (`STZ $1801`), which would have capped addressable offsets at 255 even with
   the right base.
2. **The hit-test compare was 8-bit.** `TYA; CMP $0339` moves only the low byte
   of a 16-bit `Y`, so resolution wrapped every 256 cells. The pointer never
   exposed this because it rejects rows >= 8. Now a real 16-bit `CPY $0339`.
3. **`STX $0341` (16-bit) already owned `$0341` AND `$0342`, and the found flag
   sat at `$0342`** — so every hit overwrote the character index's high byte.
   Flag moved to `$033b`. Fourth instance of this register-width family of bugs.
4. **The main loop closed with a hand-computed relative branch,**
   `b(0x80, byte(int8(loop-(back+2))))`. Adding the caret snapshot pushed it past
   -128 and `int8()` silently wrapped, landing the loop mid-instruction — it
   surfaced as an unrelated typing test failing. Now an absolute `JMP`, which has
   no range limit. **Any other hand-computed displacement in this file is a
   latent version of the same bug; the `branch()` helper panics instead.**

The cartridge static-data layout moved another `+0x100` (cumulative `+0x500`)
to fit the shared resolve subroutine and vertical movement. After the later help
plane shift and current deterministic rebuild, the image uses 554 tiles and has
76 slots left before `scanMapOffset`.

### Completed in the previous pass (editing correctness)

Three real defects found and fixed, each verified by building the ROM both with
and without the fix and confirming the new test fails then passes.

1. **Word-fit 8-bit addition overflow (the item this pass was chartered to
   close).** The fit check summed `column + wordLen` in an 8-bit accumulator.
   The word counter saturates to 31 only on the frame its own `INC` wraps
   255->0, so it legally holds up to 255 and the *sum* could wrap independently:
   a 240-character unbroken word at column 16 summed to exactly 256, compared
   below 31, and was drawn mid-line. Fixed by rejecting any word of 31+ cells
   before the addition (it cannot fit at any nonzero column regardless), which
   keeps the new comparison once per word rather than in the per-character
   measure loop the file documents as timing-sensitive.

2. **Dead screen-full bound in the word-wrap padding loop (new discovery).**
   `renderDocument` runs under `REP #$10`, so index registers are 16-bit and
   `CPY #imm` is a three-byte instruction — but `advanceToLine`'s bound was
   emitted as the two-byte `c0 f0`. It swallowed the following `BCC` opcode,
   decoded as `CPY #$90F0`, and turned the intended
   `BCC screenNotFull / JMP screenFull` guard into two garbage instructions
   (`ORA $4c,S`, `AND [$9f],Y`) whose only effect was clobbering A, which the
   next `LDA $0a` reloaded. The defect was therefore completely invisible: the
   loop still terminated on column 30. With no live bound, padding could carry
   the output cursor up to 29 cells past the 510-cell plane and the following
   draw wrote out of bounds — confirmed by observing a glyph land at `$11FE`,
   one cell past the plane, with the worst case reaching `$1216` inside the
   attribute plane. Now emitted as a correct three-byte `CPY #$01FE`, matching
   the bound `drawLoop` itself uses rather than the stale 240 (8-row) value from
   before the plane grew to 30x17.

3. **Caret cell / word-counter zero-page collision — the third collision of
   this exact shape in this file, after `$19`/`$1b` and `$09`.** Because index
   registers are 16-bit in `renderDocument`, `positionCursor`'s `STY $0b`
   caret capture writes a **16-bit** cell across `$0b` *and* `$0c`, and
   `drawCursor` reads it back the same way with `LDY $0b`. The word-length
   counter also lived at `$0c`, and every word measured after the caret was
   captured re-zeroed and re-counted it, destroying the caret cell's high byte.
   The glyph then went to `$1000 + (lastWordLen<<8) + column` — outside the
   document plane — so **no caret was drawn at all for any cursor except one
   sitting at the natural end of text**, which is the only path that captures
   `$0b`/`$0c` immediately before `drawCursor`. Confirmed concretely: cursor 8
   in a 61-character document put the caret glyph at `$1608` instead of `$1008`,
   with `$0b=$08` correct and `$0c=$06` left over from measuring the trailing
   word "juliet". Fixed by moving the word-length counter to `$5a` (confirmed
   free), leaving `$0b`/`$0c` as a clean 16-bit caret pair.

   This also **resolves the open 2026-07-26 observation** that the caret
   "appeared to stay at the document end rather than tracking the active edge of
   an in-progress shift+arrow selection." The host was never at fault —
   `makeViewport` publishes `m_cursor.position()`, which for a selection is the
   active edge. Shift+arrow simply moves the cursor off the natural end of text,
   which is exactly the case that lost the caret. There is now permanent
   coverage for the selection case specifically.

### Discoveries that constrain future work

- **Index registers are 16-bit throughout `renderDocument`** (`REP #$10` at its
  entry, `SEP #$30` at the VBlank handoff), while the accumulator is 8-bit.
  Any `CPX #`/`CPY #`/`LDX #`/`LDY #` immediate in that region must be emitted
  as three bytes, and every `STX`/`STY`/`LDX`/`LDY` to a direct-page address
  touches **two** consecutive bytes. Both defects 2 and 3 above are direct
  consequences of this being overlooked. Audit any existing or new direct-page
  pair in this region against it — `STX $58` likewise owns `$58`/`$59`.
- ~~The pointer hit-test cannot resolve past cell 255.~~ Fixed: 16-bit
  `CPY $0339`, and the pointer's stale `CMP #8` row rejection is gone too, so
  clicks now reach all 17 rows.
- **The document plane is 30x17, not 30x8.** Several comments and bounds still
  said 8 rows, long after the plane grew. It DMAs 17 rows (`LDX #$11`) to tilemap
  row 10, so on screen it spans y 80..215. Treat any remaining "8 row" reference
  in this file as suspect.
- **Never hand-compute a branch displacement in this file.** The main loop used
  `b(0x80, byte(int8(loop-(back+2))))`, which silently wrapped once the body grew
  past -128 and sent execution into the middle of an instruction. It surfaced as
  a completely unrelated typing test failing. The `branch()` helper panics on
  out-of-range; prefer it, or an absolute `JMP` when range is genuinely unbounded.
- **The pointer scratch comments are wrong by 6 for the `$034x` block.** The
  code comments say `$033a`/`$033b`/`$033c`/`$033d`/`$033e`/`$033f`, but the
  emitted bytes are `$0340` (hit active), `$0341` (found cell), `$0342` (found
  flag), `$0343` (column), `$0344` (command kind), `$0345` (last emitted cell).
  Trust the bytes, not those comments.
- **Free scratch confirmed by a full direct-page opcode scan of the ROM
  source:** `$5a`+ in zero page (note `$09`, `$51`, `$53`, `$55`, `$57`, `$59`
  only *look* free — they are the high halves of existing 16-bit pairs), and
  `$0313`, `$0314`, `$031a`-`$031c`, `$032b`, `$032e`, `$032f`, `$0333`,
  `$033a`-`$033f`, `$0346`, `$0347`, `$0350`, `$0351` in the `$03xx` page.
- **CMake does not reliably regenerate the cartridge from `main.go` on this
  volume.** Timestamp comparison silently skipped regeneration more than once
  during this session, producing test results for the wrong ROM. Always
  `touch tools/fairywriter-rom/main.go` before building, and confirm the
  intended bytes are actually in `build-arm64/fairywriter.sfc` before trusting a
  pass or a failure. Note also that building only the `fairywriter_cartridge`
  target regenerates the `.sfc` but not the embedded copy, which trips the
  `fairywriter_cartridge_contract` test (exit 54); build
  `fairywriter_snes_machine_tests` to refresh both.
- **`snes_machine_tests.cpp` return codes must stay below 256.** A process exit
  status is the low 8 bits of `main`'s return, so a `return 256` reports success.
  This silently masked a genuine reproduction during this session.

### Completed in the earlier development pass

- Fixed caret screen placement and the masked caret attribute-clobber bug.
- Wired Page Up/Page Down through cartridge command dispatch.
- Fixed the 8-bit word-length counter overflow and the `$09` zero-page collision
  that truncated documents over 255 characters.
- Fixed document-plane clearing to cover all 30x17/510 staged cells with a
  throughput-safe paired 16-bit loop.
- Added a real cartridge-owned position thumb using OAM sprite 1 and committed
  `bytes_before / total_document_bytes` metadata.
- Kept thumb OAM writes event-driven. Rewriting it every VBlank caused measurable
  input/render timing regressions.
- Removed unused private host-font resources, notices, and personal artifact
  variants from macOS and Linux production paths.
- Fixed the Linux-only real-ODT test assumption: the source tree is mounted
  read-only, so the test copies the real fixture into a writable temporary
  workspace before mutation.
- Pruned roughly 435 MiB of obsolete generated builds, app snapshots, loose
  ROM/save files, and stale archives into a recoverable Trash folder. The
  scratchpad was restored after its durable-memory role was clarified.

### Current validation evidence

- `GO111MODULE=off go test ./tools/fairywriter-rom` passes.
- Native `ctest --test-dir build-arm64 --output-on-failure` passes 10/10 with
  the new SNES-machine regressions, now also covering a click below row 8, the
  optimistic cursor agreeing with the published position, and the guest cursor's
  high byte surviving a keystroke: word-fit addition overflow, out-of-plane
  word-wrap padding, mid-document caret placement, caret-on-active-selection-edge,
  and eight wrap-aware vertical-movement cases (Down/Up one wrapped row, Up on
  row 0 falling back to semantic MoveUp, shift+Down extending, clamping onto a
  short line, sticky column surviving that short line, a horizontal move clearing
  the sticky column, resolution past cell 255, and the pointer publishing a real
  click offset).
- `fairywriter_xband_end_to_end` passes 10 consecutive repeat runs after all
  three fixes, so none of them disturbed input/render timing.
- Each of the three fixes was verified in both directions: the ROM was rebuilt
  with the fix removed and the corresponding new test observed to fail with its
  specific diagnostic, then rebuilt with the fix and observed to pass. (The
  selection-edge caret test shares the mid-document caret test's mechanism and
  is guarded by it — the mid-document test returns first on a broken ROM.)
- The scrollbar is covered on both sides. Host: the view anchors off the caret,
  the caret does not move, the out-of-view flag is set and then cleared by
  placing the caret, and typing releases the scroll. Cartridge: a track press
  publishes the expected thumb, a drag that leaves the thin track keeps scrolling
  instead of becoming a text selection (verified to fail as a selection when the
  drag-arm flag is disabled), an unchanged thumb does not republish, and the
  caret is suppressed and restored by the out-of-view flag. A framebuffer dump
  confirmed the thumb tracks the pointer across the track at both ends and centre.
- `FairyWriter.app` was rebuilt and re-synced from `build-arm64/FairyWriter.app`
  after the gate; both pass `codesign --verify --deep --strict`, the executable
  is arm64, and `--version` reports `FairyWriter 0.1.0`.
- Containerized Ubuntu/x86_64 tests pass 10/10; the AppImage passes version,
  X11, Wayland, ROM/scratch exclusion, and retired-font leak checks.
- `build-arm64/FairyWriter.app` and the synced top-level app pass strict deep
  ad-hoc signature verification.
- Canonical AppImage SHA-256:
  `81bb878256067acd2a03deada7288dd340901bdbb23323b2fd16f77c012b704a`.

### Next actions

1. **Manual verification of the signed app against the real ODT fixture.** This
   is now the single outstanding acceptance gap, and it has grown a lot of
   specific things worth looking at, all of which changed under automated proof
   but none of which have been seen by a human:
   - the caret is visible with the cursor parked mid-document and while
     shift+arrowing (it demonstrably was not, at all, before these passes);
   - clicking with the mouse puts the caret where you clicked, rather than at
     the start of the viewport;
   - `Up`/`Down` track the wrapped lines you can actually see, hold their column
     across a short line, and still scroll at the top and bottom edges;
   - no stale lower-row cells after a long-to-short viewport change, and the
     position thumb travels start-to-end.
2. Conclusively verify existing toolbar behavior before expanding alignment or
   list scope.
3. The F1/SAVE/BACK hint bar is gone with the footer. If its discoverability
   turns out to matter, the top status row (CHAPTER / WORDS) is the only place
   left with room, and it is already busy.
4. The scrollbar has not been seen by a human yet. A framebuffer dump confirms the
   thumb tracks the pointer across the track, and the host/cartridge halves are
   covered separately, but the full round trip -- drag, watch the text scroll,
   click to place the caret -- has only been proven in pieces. Worth being the
   first thing checked in the manual pass.
5. Consider auditing the remaining 8-bit arithmetic in `editorDispatch`'s local
   handlers. `left`/`right`/`home`/`end` still operate on `$00` with 8-bit
   opcodes, so their optimistic frame is wrong for documents over 255 characters
   in the same way Up/Down was. Not yet measured, and the host corrects it on the
   next commit, so this is a polish item rather than a defect report.

### Maintenance checklist for future agents

- Update the date and current resume point after each meaningful session.
- Record the real invariant, root cause, final structural fix, and failed
  approaches that constrain future work.
- Keep branch/dirty-tree state, artifact freshness, validation results, manual
  acceptance gaps, and the single next action current.
- Correct stale statements in place or move them under the archive. Never delete
  this file during repository cleanup.
- Do not treat signed bundles or automated tests as proof of live visual/input
  acceptance.

---

## Archived legacy desktop-presentation history

The material below is retained because it records useful architectural
decisions, platform failures, and rejected approaches. Its product state and
artifact references are historical unless promoted into the current resume
point above.

> **2026-07-26 — track pivot.** This scratchpad below documents the LEGACY
> SNES-themed Qt desktop app (wallpaper/theming). The ACTIVE product is now the
> real **SNES-ROM word processor** (native binary via snesrecomp). For current
> work read, in order: [`plan.md`](plan.md),
> [`DEVELOPMENT_STATUS.md`](DEVELOPMENT_STATUS.md), and
> [`docs/HANDOFF_2026-07-26.md`](docs/HANDOFF_2026-07-26.md) (the exact resume
> point). Confirmed scope: native-only (never real hardware), no emoji/glyph
> streaming, WordPerfect-for-DOS simplicity (one font; bold/italic/underline;
> red spelling underlines; green grammar underlines). The cartridge track was
> committed on branch `landmark1-viewport-wireformat`.

### Legacy immutable references

- Product specification: `plan.md` (change only after a manually approved Plan Mode revision).
- Upstream base: FocusWriter 1.9.0, GPL-3.0-or-later.

### Legacy invariant

Preserve FocusWriter's real document/session/recovery behavior while replacing presentation through explicit, centralized state. No pure-black or unpainted presentation pixels may reach the FairyWriter window. Document geometry, UI control magnification, and UI type size are independent: the current default is 2x controls with 18pt UI type, and every selected side-panel page must fit the fixed SNES tool viewport without a whole-dialog scrollbar.

### Legacy active task

Theming refinements and the animated cherry-blossom/meadow wallpaper are implemented with all six suites passing. The remaining acceptance step is a manual visual pass: cherry motion (petals behind paper, drifting clouds, grass shimmer, swaying godrays), the Preferences "Animate wallpaper" toggle, freeze on deactivate/minimize, and Retina/Steam Deck packaging reruns.

### Legacy discoveries

- The source tree was extracted without Git history.
- The approved plan already existed as uppercase `PLAN.md`; this filesystem resolves `plan.md` to it, but the tracked spelling must be normalized.
- The installed `/usr/local` Qt 6.2.2 is x86_64 and reaches every compile step on this arm64 Mac, then fails linking against the removed AGL framework.
- Filgaia is non-commercial and the Yoshi font has no accompanying license file; both remain personal-only, ignored local assets.

### Legacy validation log

- Unmodified FocusWriter configure and compilation reached the final macOS link step.
- No repository existed before this implementation run.
- Imported FocusWriter 1.9.0 baseline committed as `6e2c748` on `main`.
- Native arm64 Qt 6.11.1 is installed under `/opt/homebrew`; translated CMake/Qt configurations are rejected before generation.
- FairyWriter identity/toolchain checkpoint committed as `465c7b6`.
- The presentation path now renders full-window generated meadow wallpaper, SNES-framed corrected document geometry, and six wallpaper modes through BGR555 preprocessing.
- `fairywriter_core` and `fairywriter_presentation` pass offscreen; Retina output, full coverage, quantization, transparency fallback, and pure-black rejection are scanned.
- Personal Yoshi/Filgaia embedding is opt-in; a separate default public build proves their payload names are absent.
- Focus Presentation now owns one 1.5-second hide timer and one endpoint-stopping step timer; header and footer cannot diverge.
- Four ordered Bayer steps cover chrome with pixels from the already-quantized presentation background; no opacity effect is used.
- Top and bottom eight-virtual-pixel zones, Alt/F10, menus, pin, and auto-hide settings feed the same controller.
- App-owned tools now mount in a scrollable 192-virtual-pixel side panel with an eight-pixel gutter; left/right placement is saved and the document group is recentered at the same integer scale.
- Find/replace, symbols, dictionaries, themes, preferences, locale, sessions, timers, and daily progress route through the shared panel host.
- Theme color buttons now snap every edit to 5-bit-per-channel BGR555, expose Red/Green/Blue 0–31 sliders, a snapped source picker, and an exact BGR555 readout; the editor exposes shell mode, UI font/size, black replacement, and frame palette colors.
- First launch now offers a one-time copy-only FocusWriter profile import into FairyWriter's isolated data directory; existing destination files are preserved and the source is never removed.
- Presentation tests now render both left and right side-panel layouts and scan their complete output for quantized, nonblack pixels.
- First UI-density pass raised Pastel Meadow/Hunter Terminal to 14pt and toolbar icons to 32px; later visual feedback superseded those exact defaults below.
- Revised after macOS visual feedback: chrome no longer consumes the fixed document's scale budget. A 2048x1152 canvas now selects scale 5 (1493x1120 corrected document) instead of scale 4 (1195x896).
- SnesStyle metrics, borders, menu spacing, small/large icons, and toolbar icons now derive from the active document virtual-pixel pitch; bundled theme defaults are 18pt.
- All three suites pass after the geometry revision; native FairyWriter remains an arm64 Mach-O.
- Preferences now uses a narrow category selector plus one page viewport instead of a desktop tab strip. It fills the side-panel host, with no whole-dialog horizontal or vertical scrollbar; only the selected category page may scroll internally.
- UI magnification is independently selectable at 1x, 2x, or 4x from View and persists under `Presentation/UiMagnification`; 2x is the default and scales fonts, icons, borders, spacing, and control metrics.
- MMX intro references clarified that the page/framebuffer and its writing grid have separate scale. Pastel Meadow and Hunter Terminal now use an eight-source-pixel document font and a 24-source-pixel inset; font, tabs, paragraph spacing, and margins all scale with document pitch so character count and composition remain stable when a panel changes the integer scale.
- `Text/VirtualPixelHeight` is separate from legacy QFont point size, avoiding oversized migrated themes while keeping the source grid editable in the theme panel. Timing acceptance now waits on the real fade endpoint rather than a narrow scheduler-dependent sleep.
- Three test executables pass; the real-timing focus suite completes in roughly 2.4 seconds.
- The scaled side-panel tools were structurally corrected rather than clipped: Preferences is split across 14 bounded pages, Themes across two pages, and the theme editor across nine pages including a live Preview page.
- The 2x interface default now renders controls at 2x while keeping UI text at a readable 24pt; 4x remains user-selectable without coupling document scale to desktop DPI.
- The attached macOS crash report was from an intentionally invalid `QT_QPA_PLATFORM=offscreen` probe against a Cocoa-only bundle, failing during Qt platform integration before FairyWriter runtime code. `--version` and `-v` now return before constructing `QApplication`, so GUI-independent queries work even with an invalid platform backend.
- Native macOS regression is 4/4 tests. Root `FairyWriter.app` is arm64, version 0.1.0, ID `io.github.navjack.FairyWriter`, has only bundled/system dependencies, passes strict deep ad-hoc signature verification, and returns `FairyWriter 0.1.0` with an intentionally invalid Qt platform.
- Linux packaging now builds inside Ubuntu 22.04 x86_64 with Qt 6.2, runs all four tests, deploys the offscreen and XCB plugins, embeds the personal Yoshi and Filgaia fonts only for the personal build, and proves the AppImage can return its version headlessly.
- The read-only-source Linux build exposed metainfo translation writes into the source tree; `TranslateMetainfo.cmake` now stages `.po` and `LINGUAS` inputs in the binary directory before translation.
- The current Steam Deck package is `FairyWriter-0.1.0-personal-steamdeck-x86_64.tar.gz` (37M, SHA-256 `b72f590e93a112a392ba8272193db9001b4fe6a1f8292b54d900f89bf5dad165`). Its streamed AppImage matches the packaged SHA-256 `678814c76b43a1b458a8ad6b730cab93051f150ce23d0d8c1415d86d9020945f`; the archive includes the personal font notice/license companion files and excludes scratch notes and ROM payloads.
- Retina feedback showed 24pt Yoshi text still consumes too much of a 192-source-pixel panel. At 2x, controls retain their doubled pitch while UI typography stays at 18pt; 4x uses 24pt.
- Preferences is now 17 bounded pages, Themes uses a category/theme selector plus one responsive preview instead of a scrolling thumbnail grid, and Theme Editor is 14 bounded pages.
- `--verify-ui-layout` now drives these tools through a real `Window` offscreen, renders every page, and rejects scroll ranges, escaped tool geometry, clipped controls, or unreachable command rows. The Linux packager requires all 33 screenshots from the deployed AppImage.
- Crash report `01B8CAE0-3B72-4102-9F4B-E8910BA90CA5` proved a destroyed Preferences widget remained in `PanelToolHost` before Themes opened. The host now releases destroyed tools; the exact audit sequence completes without a crash and all five native tests pass.
- The document/panel screenshot defect had two owners fighting over one widget: `PresentationGeometry` set the corrected paper rectangle, then FocusWriter's inherited full-window `QGridLayout` overwrote it. The document stack is no longer layout-managed; it owns exactly the frame interior, while `Document` owns only scaled page padding.
- The runtime audit now types a long real paragraph, records the full document grid, opens Preferences, and proves the editor narrows and moves with the document, remains disjoint from the panel, preserves the fixed SNES source composition, and restores exactly when the panel closes.
- Stale async theme renders are rejected unless theme, foreground rectangle, and physical image size still match the current presentation state.
- Legacy `Foreground/Opacity` is restored inside the SNES paper and the composite is requantized to BGR555. This fixes Bitter Skies' former white-on-opaque-white document while preserving Enchantment's document text color.
- UI text is now a separate theme color (`Presentation/UiText`). `SnesStyle` selects readable quantized text for each palette surface, and no longer resets newly polished widgets to Pastel Meadow. Every bundled theme passes 4.5:1 window, field, button, highlight, and tooltip contrast checks.
- Ubuntu Qt 6.2 exposed an invalid constructor-order connection that Qt 6.11 only warned about: `Window` connected `m_documents` before constructing it. The connection now occurs immediately after stack creation.
- Deterministic process teardown exposed two older lifetime defects. `Window` now stops its document-cache thread before destruction and explicitly destroys the document stack while `DocumentWatcher` is still alive, eliminating both the running-thread abort and watcher use-after-free.
- Document and UI font form rows now always wrap their labels above the selector, giving long personal font names the full side-panel width on both Qt 6.2 and Qt 6.11.
- Native arm64 and Ubuntu 22.04 x86_64 each pass all five tests. The packaged AppImage also passes its own version and 33-frame real-window layout audit after extraction.
- Final root `FairyWriter.app` is arm64, version 0.1.0, ID `io.github.navjack.FairyWriter`, strictly deep ad-hoc signed, free of Homebrew runtime loads, and passes the final deployed offscreen audit.
- Final root Steam Deck archive SHA-256 is `37eda876fc3b6f8e448933380681442c5ba4717c71a789a96d2a289b85e0f490`; its x86_64 AppImage SHA-256 is `8c729ee419faa2b856b4e136ed24427e5cc49f703ab91685ffa78e06abf652d1` and matches the packaged checksum.
- Final root macOS DMG SHA-256 is `51ab350dda9c17abcc7113f02cd23a49daa2896323e74519f7b8ae1e335003cb`; `hdiutil verify` reports a valid image checksum.
- Crash report `02185459-B3ED-4DB3-BF77-673061384AB1` is from the rejected 05:09 staging probe. That probe loaded Homebrew and bundled Qt simultaneously before platform initialization. It never replaced the root app; the corrected final app loads no Homebrew library.

- Theming refinement checkpoint: the unreachable legacy body of `Theme::render` and the private-Qt `qt_blurImage` dependency are gone (manager icons now use a flat quantized drop shadow); `reload()` documents its deliberate two-stage quantize-then-tone-map pipeline, guarded by `testBundledPaletteStability` golden palettes — collapsing to a single pass shifted every bundled palette (gentleblues UI text drifted #211931→#634200) and was reverted; never-rendered legacy fields are quarantined behind import-compat banners; the synchronous resize fallback is flat and opaque with no antialiased rounding; the theme editor history push is one helper; the focal-point control caches identical drafts and hides its crosshair for generated scenes; theme-manager thumbnails render asynchronously with load-color placeholders and no UI-thread `waitForFinished`/`processEvents`.
- Animated Presentation checkpoint (plan revised in Plan Mode): the generated scenes accept an integer tick — cherry gained pastel parallax clouds, palette-cycled grass shimmer and blossom glimmer, seeded falling petals, and Bayer-dithered godrays swaying from behind the tree; the meadow gained cloud parallax, shimmer, and cream godrays. `WallpaperMotionEngine` (80 ms ticks, QtConcurrent frames, generation-guarded staleness, tick-preserving freeze) feeds a `WallpaperMotionLayer` masked to the wallpaper region between the static background and the transparent editor, gated on generated source + opaque paper + `Presentation/WallpaperMotion` + active unminimized window. All six suites pass, including per-tick determinism, tick-zero equivalence, palette/no-black invariants at arbitrary ticks, untouched paper, a 40 ms frame budget, and engine lifecycle tests.

### Legacy next action (superseded)

Manual visual/input pass on the animated wallpaper (Retina macOS and Steam Deck): motion quality at 1x-5x scale, toggle behavior, freeze/resume on app switch, then refresh the packaged deliverables. Record feedback in a new implementation checkpoint without changing `plan.md` unless the plan is manually revised in Plan Mode.
