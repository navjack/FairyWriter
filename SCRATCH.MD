# FairyWriter Implementation Scratchpad

> **Durable working-memory contract.** This file is FairyWriter's external
> working brain. It exists specifically to survive agent context compaction and
> handoffs. Every agent continuing development must read it before editing and
> update the current sections after meaningful discoveries, decisions,
> validation, artifact changes, or changes to the next action. It is not a
> generated file, not disposable scratch output, and must never be pruned merely
> because some entries are historical. Move superseded detail into the archive
> instead of deleting the file.

## Current resume point — 2026-07-31

### Product and ownership invariants

- The active product is a native app running an embedded, source-generated SNES
  cartridge. It is not the legacy Qt editor with an SNES skin.
- The cartridge owns the visible 256x224 UI, resident glyphs, layout, caret,
  selection, menus, and status/position rendering.
- `DocumentEngine` and the host bridge own authoritative document/file state,
  undo/redo, recovery, statistics, and viewport metadata.
- A document transition is complete only after the immutable content snapshot
  and its recovery state have produced a typed durable result. The UI thread
  owns the live document; one FIFO persistence worker owns filesystem reads,
  hashes, encoding, sync, atomic commit, and recovery rotation.
- New documents are ODT. Save As offers ODT, DOCX, RTF, and Markdown; FODT and
  plain text remain compatibility formats. Markdown source is authoritative
  UTF-8 and is parsed by the pinned vendored cmark-gfm snapshot.
- Recent-file paths exist only in private host settings. A fresh `FileCatalog`
  validates them and rebuilds process-local opaque IDs before the cartridge
  requests Recent; filesystem paths never cross the SRAM mailbox.
- The SRAM mailbox is the authority boundary. Keyboard input reaches the
  cartridge through the XBAND controller-port-2 path.
- Production has one visual identity and one resident cartridge glyph set.
  Yoshi/Filgaia Qt font embedding and the associated notice/artifact variants
  have been removed.

### Read order

1. `SCRATCH.MD` — live working state, recent discoveries, and failure history.
2. `plan.md` — the design document for the pass currently in flight. It is
   rewritten per pass and is **not** a standing roadmap; as of 2026-07-31 it holds
   the SPC700 audio design delivered in 0.3.0. Read it for the reasoning behind
   recent work, not for what happens next — that is *Next actions* below.
3. `DEVELOPMENT_STATUS.md` — implemented behavior and open work.
4. `docs/HANDOFF_2026-07-26.md` — exact resume and validation protocol.
5. `docs/SNES_FRONTEND.md` — guest/host ownership and frontend invariants.

### Active working tree

- Branch: `agent/release-0-3-5`, branched from public `origin/main` at `f4e289f`,
  carrying only the 0.3.5 release cut: `VERSION`, `CHANGELOG.md`, the AppStream
  release history, and this file plus `DEVELOPMENT_STATUS.md`. No product code.
- `origin/main` is at `f4e289f`. PR #9 (render consistency), #10 (self-recorded
  showcase video), #11 (documentation hygiene), #12 (Qt UI removal), #13 (showcase
  retirement) and #14 (recorder removal) all merged on 2026-07-31 with all three
  platform jobs green.
- **This tree now has one build configuration.** There is no
  `FAIRYWRITER_DEVELOPMENT_UI`. `FAIRYWRITER_DEVELOPER_TOOLS` is a different gate
  and still exists — it builds the standalone `fairywriter_snes_player` and
  `fairywriter_xband_player` cartridge hosts, which are bring-up tools, not tester
  builds.
- Product work happens in a worktree branched from `origin/main`, never in the
  legacy checkout that hosts this repository's `.git`. That local `main` is an
  unrelated private history containing packaged applications and personal
  documents; it must never be checked out for product work and never pushed.
  Merge server-side, without `--delete-branch` — that flag switches the parent
  checkout to its default branch on its own.
- **No open manual actions.** The showcase video was retired on 2026-07-31 rather
  than re-uploaded, and the recorder that produced it was removed with it; see the
  pass note below for the reasoning and the embed-form evidence.
- The former private ODT fixture is not in the publishable source.
  `tools/public-test-fixture` generates a deterministic substantial ODT for the
  same production import/edit/round-trip test.
- A pinned `snesrecomp` bootstrap plus a checked-in XBAND keyboard/Super NES
  Mouse patch replaces any dependency on a neighboring checkout.
- Local documents, packaged applications, and ignored personal reference artwork
  stay outside Git and were not touched.
- Package hashes and build-directory state from earlier passes are historical;
  they are recorded under the pass history below rather than here, because they
  do not describe the current tree.

### Version state

- `VERSION` is `0.3.5`. `CHANGELOG.md` and the AppStream release history carry
  the same number and date (2026-07-31). `VERSIONING.md` is the authority on how
  the number moves: MINOR before 1.0 for new user-visible behavior or substantial
  cartridge/runtime change, PATCH for fixes and packaging corrections. 0.3.5 is a
  PATCH scope — render and editing fixes plus a source-tree removal that changes
  no shipped byte. **The jump past `0.3.1`–`0.3.4` is deliberate, not an error:
  Jack chose the number, and those versions were never published.** Do not
  "correct" it to 0.3.1 later.
- Releases published, all flagged prerelease: `v0.1.0` (2026-07-29), `v0.2.0`
  (2026-07-30), `v0.3.0` (2026-07-31), `v0.3.5` (2026-07-31). The version stays at
  the latest public release during ordinary development — development builds are
  identified by their commit and Actions run, not by bumping `VERSION` as a side
  effect of repository state.
- `CHANGELOG.md` has no **Unreleased** section. Everything that stood in it —
  the render-consistency fixes, the Qt UI removal, the showcase retirement — went
  out as 0.3.5. Re-add the heading when the next unreleased entry is written.

### 2026-07-31 — cut the 0.3.5 release

A release cut, not a code change. The diff is `VERSION`, `CHANGELOG.md`, the
AppStream release history, `DEVELOPMENT_STATUS.md` and this file. No file under
`src/`, `tools/` or `packaging/` is touched, so the product this release ships is
byte-for-byte the product that `origin/main` was already building green.

- **Why now.** Six PRs (#9–#14) merged on 2026-07-31 and none of them shipped.
  `CHANGELOG.md`'s **Unreleased** section had accumulated the whole
  render-consistency pass, the Qt UI removal and the showcase retirement while
  `VERSION` still read `0.3.0`, so every artifact CI produced was named for a
  release that did not contain any of it.
- **Why 0.3.5.** Jack's number. Scope-wise `VERSIONING.md` puts this at PATCH —
  fixes plus a source-tree removal that changes no shipped byte — and the jump
  past `0.3.1`–`0.3.4` is intentional rather than a miscount. Recorded here
  because the gap will otherwise read as an error to a later agent.
- **What CI does and does not do.** `.github/workflows/ci.yml` runs on push to
  `main` and builds the three platform packages, naming each from `VERSION`. It
  **does not create a GitHub Release, and it does not react to a tag.** Tagging
  `v0.3.5` and publishing the release against the retained artifacts is steps 7–8
  of the `VERSIONING.md` checklist and stays manual.

### 2026-07-31 — retire the showcase video and its recorder

Decision, not a defect fix: **the README no longer embeds a video.** Jack's call,
after the cost of keeping one current was measured rather than assumed.

- **What was actually live.** The README's player was serving a 62.18-second file
  with *no audio stream at all*, while the `docs/media/fairywriter-showcase.mp4`
  then in the tree was 63.30 seconds with an AAC track. A showcase whose feature
  is sound was being represented by a silent recording of a two-version-old
  build. Confirmed by pulling the asset GitHub actually serves and probing it, not
  by reading the note that said it was stale.
- **Why it could not be automated.** Every candidate embed form was pushed to a
  throwaway branch and the rendered DOM inspected: `<video>` and `<source>` are
  stripped to zero elements, `![](…)` on an MP4 yields a broken `<img>`, and bare
  or linked repository paths render as plain links. GitHub's own file view does
  not play it either. Only a `github.com/user-attachments/assets/…` URL produces a
  player, and creating one has no API — it needs a drag-and-drop into a comment
  box in a signed-in browser. The forms tested, and what each rendered as:

  | Form | Rendered as |
  |---|---|
  | `<video src="https://raw.githubusercontent.com/...">` | stripped entirely |
  | `<video><source src="..."></video>` | stripped entirely |
  | bare `raw.githubusercontent.com` URL | plain link |
  | `![](raw URL)` or `![](relative path)` | broken `<img>` pointing at an MP4 |
  | bare or linked repository path | plain link |
- **Why retire rather than refresh.** Refreshing is a manual step with no API, so
  every future release would either repeat it or let the player drift again. It
  had already drifted. A stale player misrepresents the product worse than no
  player does.
- **Nothing stays.** The first cut of this decision kept `--showcase-record` and
  the MP4, on the reasoning that the recorder was deterministic, worked, and
  might suit a one-off capture. Jack's call was to drop it as well, and that is
  the more honest position: with no video in the README, the recorder had no
  consumer, and keeping 496 lines of working code against a hypothetical future
  use is how the retired Qt UI came to be three quarters of `src/`. Removed:
  `docs/SHOWCASE.md`, `docs/media/fairywriter-showcase.mp4`, the
  `--showcase-record` child mode with its `ShowcaseRecorder`, storyboard and
  scancode resolver, and the three `persistenceTest*` frame/audio accessors that
  only it called. `git log` has all of it if it is ever wanted.
- **What the removal did not touch.** The `--persistence-e2e-child`,
  `--persistence-cartridge-e2e-child` and `--function-key-e2e-child` modes share
  the same `FAIRYWRITER_PERSISTENCE_TESTING` block and are untouched; `ctest`
  stayed 11/11 across the removal.
- **The general lesson**, worth applying beyond this file: a documented "manual
  step required" is a recurring cost, and recording it in a handoff note does not
  pay it. This one sat unpaid long enough to go stale and misrepresent the
  product. Prefer removing the obligation to documenting it.

### 2026-07-31 — remove the retired FocusWriter Qt UI

Invariant: this tree builds one thing. The cartridge product was already the only
shipping configuration; what remained was a second, non-shipping configuration
that nothing gated against and that no CI job exercised.

- **556 files, ~122k lines deleted.** 114 of the 183 files in `src/` were
  reachable only from the `FAIRYWRITER_DEVELOPMENT_UI` target; 69 remain. Plus 390
  files under `resources/` (themes, sounds, symbols, the Oxygen icon set), 46
  translation catalogues, 3 legacy test suites, and the Blasphemous font.
- **The removal is invisible.** The built `FairyWriter.app` is identical file for
  file before and after — `Info.plist`, the executable, `fairywriter.icns`,
  `_CodeSignature`. That was the acceptance criterion and it is worth keeping in
  mind: `cmake/PruneProductionBundle.cmake` had been deleting this material back
  out of the bundle at build time, so the shipped artifact never contained it.
- **How the dead set was derived, and the one trap.** An `#include` closure from
  every `src/` path CMakeLists still names, seeded also from `tests/`. Two seeds
  had to be removed first or the closure kept the whole legacy tree alive:
  `set_property(SOURCE src/main.cpp ...)` for `FAIRYWRITER_DATADIR`, and a stale
  reference in `fairywriter_core_tests`. The closure then produced one **false
  positive**: `src/snesrecomp_support/variables.h` looked unreachable because it
  is included by the *vendored* `third_party/snesrecomp/runner/src/snes/{cpu,snes}.c`
  through the include path at `CMakeLists.txt:292`, not by anything under `src/`.
  **Any future dead-code sweep must scan `third_party/` includes too**, or it will
  delete that header and break the machine build.
- `presentation_geometry.cpp` and `snes_color.h` are *not* legacy despite living
  beside it — `fairywriter_core_tests`, one of the production 11, uses both.
- **What `PruneProductionBundle.cmake` still does.** The plan for this pass said
  it could be deleted. That was wrong: `mac_deploy.sh:100` runs it *after*
  `macdeployqt`, which copies Qt's own `*.lproj` localizations in even under
  `-no-plugins`. The POST_BUILD invocation cannot see those. The script is kept,
  reduced to the `*.lproj` sweep, with the reason written into it.
- **Dependencies dropped**: Qt Concurrent, LinguistTools, Multimedia, Network and
  PrintSupport; Hunspell and Voikko via `pkg_check_modules`; KDSingleApplication.
  Linux packaging no longer needs a spell-checker present to configure.
- `CREDITS.md` is untouched, translators included. FairyWriter is still a
  GPL-3.0-or-later derivative of FocusWriter 1.9.0 and deleting UI code does not
  retire that attribution. `THIRD_PARTY_NOTICES.md` lost only the two entries
  whose assets left the tree (Oxygen, Blasphemous) and says so explicitly, so the
  absence does not read as an oversight.
- Validation: clean configure from an empty build directory (a stale
  `CMakeCache.txt` would hide a removed dependency), `ctest` 11/11, and the full
  `./mac_deploy.sh` gate — build, tests, Qt deploy, prune, ad-hoc sign, the
  arm64/version/no-Homebrew-leak/no-loose-ROM audits, and a verified DMG.

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
  **This list has since decayed** — `$0333` is the scroll-drag flag and
  `$0350`/`$0351` the pointer's last emitted cell. In zero page `$5c`/`$5d` are
  *not* free either: the sound plane's render and input both use them. Re-scan
  before claiming a byte rather than trusting this list.
- **The per-cell arrays the draw loop reads are `$0500` (text), `$0b00`
  (style/proofing bits) and `$0d00` (paragraph alignment), all bounded to 239
  cells and all page-aligned.** They are parallel: anything that shifts one must
  shift all three, or the optimistic frame disagrees with the host's next
  viewport and the difference shows as a flash. 8-bit `X` addresses them exactly
  as the draw loop's 16-bit `X` does, because the count never exceeds 239.
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

### Current validation evidence

**The production configuration registers 11 tests, not 10.** The older "10/10"
figure predates `fairywriter_cartridge_conformance`. Verified on 2026-07-31 with
`ctest --test-dir build -N`; the number in `BUILDING.md`, `DEVELOPMENT_STATUS.md`
and `docs/SNES_FRONTEND.md` was corrected to match in this pass. Historical
"10/10 passed" entries in the pass history below are left alone — they were true
when written.

- 0.3.5 release cut, native macOS arm64 Release from a clean `build/`: **11/11
  passed**. The number reaches the binary, not just the file — `--version` prints
  `FairyWriter 0.3.5` and the bundle's `CFBundleShortVersionString` is `0.3.5`.
  `fairywriter_cli_version` and `fairywriter_cartridge_contract` both pass against
  the regenerated cartridge (`touch tools/fairywriter-rom/main.go` first, as
  always on this volume).
- Native macOS arm64 Release `ctest`: **11/11 passed**, both before and after the
  Qt UI removal, from a **clean** build directory each time. The registered set is
  `fairywriter_cartridge_contract`, `fairywriter_cartridge_conformance`,
  `fairywriter_xband_end_to_end`, `fairywriter_core`, `fairywriter_mailbox`,
  `fairywriter_document_engine`, `fairywriter_persistence`,
  `fairywriter_persistence_process_e2e`, `fairywriter_function_keys`,
  `fairywriter_file_catalog`, `fairywriter_cli_version`.
- Full `./mac_deploy.sh` gate passes after the removal: build, 11/11 tests, Qt
  deploy, prune, ad-hoc sign, the arm64/version/no-Homebrew-leak/no-loose-ROM
  audits, and a checksum-verified `FairyWriter-0.3.0-macos-arm64.dmg`.
- The built `FairyWriter.app` is identical file for file before and after the
  removal: `Contents/Info.plist`, `Contents/MacOS/FairyWriter`,
  `Contents/Resources/fairywriter.icns`, `Contents/_CodeSignature/CodeResources`.
- GitHub Actions run `30657181192` (PR #9, render consistency): `linux-x86_64`
  2m44s, `macos-arm64` 4m1s, `windows-x64` 5m30s — all pass.
- GitHub Actions run `30657855027` (PR #10, self-recorded showcase): `linux-x86_64`
  2m46s, `macos-arm64` 3m34s, `windows-x64` 3m24s — all pass.
- Not re-run in this pass, and therefore not claimed as current: the Qt 6.8.3
  Linux container gate and the ×10 repeat of the timing-sensitive XBAND gate.
  Their last recorded results, with the package hashes of the binaries they were
  run against, are in the pass history below.

### Next actions

In order:

1. **Publish `v0.3.5`.** CI names the artifacts from `VERSION` but does not tag
   and does not create a GitHub Release. After the post-merge `main` run is green,
   tag `v0.3.5` on that exact commit and publish the release against its retained
   DMG, AppImage, ZIP and `.sha256` sidecars, flagged prerelease. Never move a tag
   or replace a published asset; correct the source and cut a new patch instead.
2. The packaged-app manual smoke on macOS, Linux, and Windows: first Save, format
   picker layout, close/relaunch/open, F3 settings/history, forced-exit recovery,
   Markdown Rendered/Source. Automated process evidence proves persistence
   semantics but not final cartridge pixels or packaged input. Windows x64 still
   awaits its first physical-host acceptance.
2. Keep the long-document caret, scrollbar, wrap, and stale-cell checks in the
   same human pass; their automated evidence is green but does not replace visual
   inspection. The 0.3.0 audio path adds its own live checks — typing latency
   with blips on, each envelope field audible, persistence across restart.

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

## Completed pass history

Newest first. These sections are superseded detail, retained per the
durable-working-memory contract at the top of this file: the current resume
point is above, and nothing here is deleted merely because it is historical.
Each heading carries the date of the pass it records.

### 2026-07-31 — self-recorded showcase video

Invariant: the showcase is generated, not captured. One emulated frame is one
video frame and one S-DSP block, so picture and sound cannot drift and the same
commit reproduces the same file.

- `--showcase-record` is a scripted child mode beside the existing
  `--*-e2e-child` entry points. It drives the same `RecompPlayer` a real session
  runs and dumps each 256x224 framebuffer as raw BGRA plus each DSP block as 800
  stereo s16 frames, which locks 48 kHz to 60 fps with no fractional
  accumulator. `FAIRYWRITER_NO_AUDIO` is set so `pumpAudio` does not consume the
  same blocks; with an output device open, each consumer would get half the
  audio.
- The storyboard is `showcasePerform`. Keystrokes go through the XBAND scan
  table and the pointer moves in bounded relative packets from the cartridge's
  own (128,112) start, so the toolbar card and the shaper's faders are
  hit-tested by the guest exactly as for a real mouse. Slider sweeps hold the
  button down and walk cell by cell, which the cartridge re-hit-tests every
  frame, so the DSP and both scopes follow continuously.
- Two failures worth not repeating, both found by looking at the frames rather
  than by any assertion. First: `DocumentEngine` refuses a style toggle without
  a selection, so "arm a style and type into it" renders nothing — the demo
  types a word, selects it back, and clicks the toolbar. The styles then stack
  on their own because each word inherits the previous word's format, which is
  exactly the 0.2.0 claim, and an added "select the line and add bold over the
  top" beat silently *un*-bolded everything because the toggle reads the format
  at the cursor and the cursor was sitting on already-bold text. Second: the
  shaper persists every edit to `settings.ini`, so the second recording opened
  on the first one's voice; the recorder now resets sound and persistence
  settings to defaults before it starts.
- The recorder runs under its own application name and a scratch catalog root,
  keeping it off the developer's real saved settings and keeping real home
  directory filenames out of a public video.
- Reviewing this kind of artifact means rendering it and looking: a waveform
  overview caught a 15-second silent hole in the middle of the first take, and a
  zoomed crop of the text row caught the un-bolding. Neither is visible in a
  pass/fail signal.

### 2026-07-31 — render consistency / no flash

Invariant: the frame the cartridge draws on its own must be the frame the host is
about to publish. The local render may be stale by a frame; it may never show a
different state. Every flash in this product is that invariant being broken.

- Reported symptom: styling and the selection blink while typing. Cause was
  `emitDocumentRenderCall`'s leading `STZ $1f`. `$1f` bit 7 is the draw loop's
  styling gate (`BIT $1f`), so every local edit *and every cursor key* drew a
  frame with rich-style glyph pages, spelling/grammar palettes and the selection
  highlight all thrown away, and the host restored them ~2 frames later.
- Keeping the flag required maintaining the state behind it. `$0b00` (per-cell
  style/proofing) and `$0d00` (per-cell alignment) are indexed by cell, so the
  three edit paths now shift them in the same loops that shift the text at
  `$0500`. An inserted cell inherits the preceding cell's style, which is what
  `DocumentEngine` publishes.
- **Delete was shifting `$0201`/`$0200`, which nothing in the ROM reads** — the
  same typo backspace's own comment records having fixed. It dropped the *last*
  character for two frames instead of the one at the cursor. The ROM contract
  test *pinned* this: `9d 00 02` labelled "guest document buffer write" sitting
  next to `bd 00 05` as the read. **When a contract test pins two halves of one
  operation, check that they name the same thing** — this one held a defect in
  place for as long as it existed.
- **A selection reaching the end of the viewport text never highlighted.** The
  decode matches a position only on a character it visits and never visits the
  offset one past the last. The cursor had an explicit post-loop catch-up
  (`LDA $46; CMP $40; BNE +2; STY $00`); the selection bounds had none. Shift+End
  and select-all land exactly there. Both bounds now get it — the start needs it
  too or a collapsed cursor at the end pairs a stale `$50` with a correct `$52`
  and highlights the whole line.
- Shift+arrow extends locally. **The anchor never needs storing:** `$00` is the
  active edge (the host publishes `m_cursor.position()`), so whichever of
  `$50`/`$52` the cursor is not on is the anchor and the other one moves.
  Crossing the anchor falls out of the same rule.
- `STA $1f` in the viewport decode ran under `REP #$20` and so owned `$1f` *and*
  `$20` — the SNES Mouse packet buffer's signature byte. Latent only because the
  poll rotates all four bytes in fresh before reading. **Seventh instance of the
  register-width family.**
- The VBlank upload waited on the *level* of `$4212` bit 7, not its rising edge.
  The upload is 48 DMA setups, about half a VBlank of CPU time, so a late arrival
  could not finish inside it — and `soundStreamRenderCall` reaches it from inside
  the main loop's own in-VBlank branch, so that path was always late. It drains
  the current VBlank first now.
- Host pacing: 17 ms (58.8 fps) against a 60.0988 Hz guest on a vsynced surface
  beat about once a second. True NTSC period now carried in a fractional
  accumulator (`scheduleNextFrame`).
- **Test-harness leverage worth reusing:** `snes_machine_tests` has no host, so
  nothing republishes the viewport and a bad optimistic frame becomes a permanent
  state instead of a two-frame transient. That is the only reason this class of
  defect is assertable at all. Seven new cases; each confirmed to fail on the
  cartridge built without its fix.
- **Rendering and looking is again what found the deepest defect.** Every byte
  assertion in the suite passed while the end-of-text selection simply was not on
  screen. A scratch tool that dumps the framebuffer crop to PPM around a single
  keystroke is cheap to rebuild — link `lookflash`-style against
  `libfairywriter_cartridge_image.a` + `libfairywriter_snes_machine.a`.

### 2026-07-30 — SPC700 audio path and shaper (0.3.0)

Recorded retroactively on 2026-07-31: the 0.3.0 release shipped without ever
being written into this file, which is the one thing the durable-memory contract
above exists to prevent. Reconstructed from the shipped commit `5193aa2` (PR #8),
not from `plan.md` — where the two disagree the shipped code is authoritative,
and the disagreements are the part worth keeping.

Invariant: nothing is synthesised on the host. Every sample the user hears was
produced by the emulated S-DSP keying a voice; the desktop side only carries
those samples to an output device.

- **The APU was already compiled in and simply never clocked.** `snes/apu.c`,
  `spc.c`, `dsp.c` and `audio_shadow.c` were in the production binary all along.
  `clock_position()` advanced the PPU and DMA only and never accumulated
  `snes->apuCatchupCycles`, so `snes_catchupApu()` always had zero cycles to run.
  The work was to clock it, drain it, and give the cartridge a driver — not to
  write an audio engine.
- **Catch up per scanline, not per frame.** The plan said once per completed
  frame; that is wrong. `snes_catchupApu` clamps its own accumulator at 10000
  cycles and silently discards the rest, while one NTSC frame is worth
  `1025280/60 = 17088` SPC cycles — so per-frame catch-up throws away 42% of the
  APU's time and permanently starves the DSP's sample ring. Per scanline is ~65
  cycles, far inside the clamp.
- **`kApuCyclesPerMaster` is restated, not exported.** `snes.c:23` keeps the ratio
  as a file-static and only `snes_runCycle` — which this machine does not use —
  accumulates it. `src/snes_machine.c` restates
  `(32040.0 * 32.0) / (1364.0 * 262.0 * 60.0)` with a comment citing that line,
  rather than patching the vendored checkout.
- **`RtlApuLock`/`RtlApuUnlock` stayed no-ops deliberately — do not "fix" this.**
  The plan called for replacing them with a real mutex. Here it would guard
  nothing: this machine has exactly one APU owner (`fairy_snes_run_frame` drives
  it, `fairy_snes_audio_read` drains it, same thread) and the host's own ring
  buffer is what actually crosses the thread boundary. Reinstate them only if the
  APU is ever advanced off the frame thread.
- **New C API**, callable only from the thread that drives `fairy_snes_run_frame`:
  `fairy_snes_audio_blocks`, `fairy_snes_audio_read` (consumes exactly one
  534-sample native block and resamples to the host rate; returns false without
  consuming when a full block is not yet buffered, so the caller emits silence
  rather than stalling the guest), `fairy_snes_audio_discard`, plus
  `fairy_snes_debug_aram` / `fairy_snes_debug_dsp_reg` for the tests.
- **Output sink is vendored miniaudio 0.11.25, not Qt Multimedia.** `QAudioSink`
  routes through the platform media-backend plugin — the ffmpeg plugin on Linux —
  and `packaging/linux/build-linux.sh` has an explicit gate that fails on
  unresolved plugin dependencies. miniaudio wraps CoreAudio/ALSA+Pulse/WASAPI with
  zero packaging change on all three platforms and matches the existing vendoring
  pattern. Unlicense or MIT-0; recorded in `THIRD_PARTY_NOTICES.md`.
- **`src/audio_output.h/.cpp`** — a lock-free SPSC `AudioRing` (power-of-two
  capacity so the monotonic counters index by mask and survive wraparound) plus a
  best-effort `AudioOutput`. A short write means the consumer is behind and the
  surplus is dropped rather than letting latency grow without bound; an underrun
  reads as silence and never stalls. **Device-open failure is non-fatal** — the
  editor runs silently. The host frame timer is 17 ms rather than a true 16.67 ms,
  so the render count is nudged toward a steady buffer fill instead of using a
  fixed block size that would drift.
- **Every wait in the IPL upload is bounded.** The cartridge uploads a real SPC700
  driver through the boot ROM's own `$BB/$AA` → `$CC` handshake. The APU is a
  separate processor and an unbounded spin would hang boot and take every headless
  test with it. On timeout `$0371` stays set as the audio-unavailable flag and the
  cartridge carries on silently.
- **Echo register order is load-bearing.** `ESA` (buffer page) and `EDL` (delay,
  in 2048-byte steps) are written *before* `FLG`'s echo-write-disable bit clears.
  Enabling writes first lets the DSP scribble at whatever page `ESA` happened to
  hold — and `ESA` pointing at the driver image would have the DSP overwrite the
  running program. Pinned by
  `TestDspInitTableReservesTheEchoBufferBeforeEnablingIt`.
- **Waveforms are source-owned.** The DSP has no oscillators, so square and
  triangle are BRR samples from a real encoder written in Go; only noise is the
  hardware's own. No Mega Man X sample data was extracted — that reference was for
  character, not content.
- **Shipped scope exceeded the plan: 12 settings, not 8.** The F5 shaper exposes
  blips on/off, waveform, attack, decay, sustain level, sustain rate, release,
  pitch, volume, and the echo unit's volume, delay and feedback — each a real
  S-DSP register field bounded by the width of the hardware field it is written
  into, not a host-side approximation. It also gained live audition (Space or
  Enter plays the voice, every edit auditions itself) and two live scopes.
- **Permanent coverage.** Go: `TestBrrWaveformsDecodeToTheShapesTheyName`,
  `TestBrrEncoderRejectsUnrepresentableInput`,
  `TestSpcImageIsSelfConsistentAndFitsItsBank`,
  `TestDspInitTableReservesTheEchoBufferBeforeEnablingIt`,
  `TestSoundPlaneRowsFitLabelSliderAndValue`,
  `TestCartridgeVoiceMirrorMatchesTheInstalledRegisters`,
  `TestSoundFieldStateBytesAreContiguousAndBounded`. C++ in
  `tests/snes_machine_tests.cpp`: driver upload and key-on; blips-off really
  silences rather than just stopping a retrigger; the register-write command path;
  a host sound-settings event travelling mailbox → cartridge state → nine
  handshaked DSP register writes; and the shaper's audition, capture, and scopes.
- **Frame timing was the standing risk.** Clocking the APU adds work to every
  emulated frame and `fairywriter_xband_end_to_end` is documented as sensitive to
  exactly that — a prior case had a slower `clearStage` silently drop queued
  keystrokes. The gate was that test ×10 before the full suite.
- **Still manual.** No automated test proves the audio is audible on real
  hardware. Typing latency with blips on, each envelope field being individually
  audible, and persistence across a restart remain live checks.

### 2026-07-29 — rich style combinations and alignment

Invariant: bold, italic and underline are independent properties of a character
and every combination has its own glyph page; paragraph alignment is cartridge
layout, applied per visual line, and the caret and the mouse follow the line it
moves.

- The cartridge had four glyph pages (plain/underline/bold/italic) and the draw
  loop chose one by priority, so underline hid bold and bold hid italic. The
  three style bits are now the page index itself: bit 0 lands in the tilemap
  character byte (+128) and bits 1-2 in the attribute byte's tile-id bits 8-9,
  which the draw loop already had free. That deleted the priority chain instead
  of adding to it.
- Eight ASCII-aligned 128-tile pages would be 1024 tiles, 32 KiB — more than the
  tile blob's 28,208-byte budget. A styled page therefore stores printable ASCII
  only (95 tiles) while still occupying a 128-id stride in VRAM, and the scene
  art moved into the styled pages' unreferenced control-character slots. 835
  tiles of the 881 the ROM can hold, and 1022 is the highest BG1 id used. The
  ROM-to-VRAM order is no longer one blob, so character data uploads as ten DMAs
  from a build-time chunk list.
- Underline was full-width ink white on the one pixel row that touches the next
  line of text, which read as a bar between two lines and made an underlined
  line above another one hard to read. It is now the palette's pale blue and
  stops a column short of the cell edge. Five variants were rendered and
  compared before choosing.
- Alignment is applied by translating each finished row inside the plane rather
  than by laying it out pre-shifted: a line's width is only known once the line
  ends, and the wrap decisions that produced it must not depend on where it is
  then placed. `finishRow` owns that, and the draw loop's own wrap arithmetic,
  cursor capture and hit-test keep walking one unshifted coordinate space.
- Three defects found by rendering and reading the plane back, not by reasoning:
  the wrap padding loop's back edge re-entered at the label where the shift was
  applied, finishing the same row once per padded cell; the row-index bookkeeping
  clobbered the X register the move loop needed, which corrupted whole rows; and
  a wrapped line counted the space it broke at, which left right-aligned text a
  cell short of the margin and cost a centred line its last character.
- `resolveCellCommand` is the one place that converts a visible cell back into
  layout space, so the pointer and wrap-aware Up/Down both got it from one
  change. It restores the caller's own target afterwards, because the pointer
  keeps that value as its already-published key and a held button would
  otherwise re-publish an extend command every frame over a shifted row.
- Coverage: the machine test pins the tilemap bytes for all eight combinations
  plus two proofing overlaps, proves all eight render distinct pixels through the
  real machine, and checks alignment's plane rows and shift table, the caret on a
  centred row, and a click on a centred row resolving to the character under the
  pointer. The ROM contract test pins the glyph-page count, per-combination glyph
  distinctness, the upload chunks against BG1's id ceiling, and the alignment
  wiring. Production stays at 10/10 ctest.

### 2026-07-29 — browser row selection

Invariant: the row the user is looking at is the row that opens. Per-row browser
tables are one byte per visible row and must never be indexed with the opaque-ID
table's 32-byte stride, and no request the user made may end in silence.

- Manual macOS acceptance after the save-flow pass reported that saving worked
  but Open did nothing. `browserEnter` read the per-row flags (`$17e0`) and
  opaque-ID length (`$17e8`) tables — one byte per row, exactly as the
  file-entry decode writes them — with `selection * 32`, the stride of the
  `$1600` ID table. Row 0 aliased correctly, so every earlier test and the
  Recent list (whose newest file is row 0) passed. Any lower row read another
  row's bytes: Enter emitted an empty or truncated ID, the host could not
  resolve it, and the cartridge had already left the browser for the unchanged
  document. Folders sort ahead of documents, so the wanted file is almost never
  row 0. Selecting a folder below the first row broke the same way, and mouse
  clicks share `browserEnter` through `$0320`. Both readers now use the row
  index; only the ID copy loop keeps the shifted one.
- `DocumentBridge::openFile` returned false for an unresolvable ID without
  telling the cartridge, which is the silence the user actually saw. It now
  publishes `EventOpenFailed`, so the outcome dialog explains the failure.
- Opening a document, restoring recovery, or switching sessions kept the
  previous document's forced scrollbar anchor and last published window, which
  could open a file at its end. A replaced document now gets a fresh view.
- Coverage for the exact reported flow: the machine test drives a multi-row
  page and asserts that the second folder row and the third file row each carry
  their own opaque ID, and the process test adds a `browse` child that walks the
  real production binary through F1 -> Open -> root -> the row the file actually
  occupies -> Enter and verifies the loaded document. Both fail on the previous
  cartridge; the `browse` child failed with the cartridge outcome dialog
  (mode `$0b`) and an empty document. Production stays at 10/10 ctest.
- Both local packages were rebuilt from this tree after the fix. The native
  macOS DMG passes 10/10 plus its signature, arm64-only, version, developer-path
  and content audits with a valid disk-image checksum, SHA-256
  `8993e37e66f56d2a3e3eaa66b8363a012797335bf22cf3a784ffea480475e741`. The
  Ubuntu 22.04 container AppImage passes 10/10 plus clean-environment version,
  every-plugin `ldd`, X11 Xvfb liveness, Wayland launch and content audits,
  SHA-256 `99a00273dc29e9128f395c7ff0cc172cdbe13c4f8af434c111aae6d380b9a312`.
- The packaged macOS artifact itself was driven through the reported flow: from
  the mounted DMG, that app's cartridge saved a real ODT, and a second fresh
  process from the same app walked F1 -> Open -> root -> the row the document
  actually occupies -> Enter and loaded it. The DMG ships only the cocoa
  platform plugin, so packaged-app checks must not request `offscreen`.

### 2026-07-29 — manual release-failure hardening

Invariant: the browser folder visible when the user presses N is the only Save
As New destination; filename mode owns one cleared focus model for keyboard and
mouse; a fresh process can reconstruct Recent without first browsing a folder;
and a release package must start without development runtime paths before it
can be called self-contained.

- Manual macOS acceptance exposed that first Save's file browser never
  explained the N action. More importantly, the cartridge copied the last
  entered folder into a second `$1840/$0330` destination buffer; Back restored
  the visible `$1900/$0348` browser context without restoring that duplicate.
  The current browser parent is now the sole authority. Root and folder titles
  explain the two steps, N is ignored at the roots page, and machine coverage
  proves Back/re-entry serializes the currently visible folder.
- The former filename prompt copied the ready-browser plane and painted three
  labels over uncleared rows. Filename entry now owns a dedicated 30x8 plane,
  a bordered 24-cell field, explicit keyboard instructions, and stacked Save
  and Cancel actions in the former formatting card. Name/Save/Cancel share one
  focus byte, Tab/arrows visibly move it, Enter activates it, and the field and
  both buttons are mouse targets.
- Qt can deliver a complete press/release between guest frames. The Super NES
  Mouse boundary now retains a pending press edge until the next controller
  latch, so a fast desktop click cannot disappear. The machine regression
  drives that exact sub-frame sequence.
- `FileCatalog` no longer treats a nonempty unknown parent ID as its root.
  Invalid or read-only Save As targets publish a visible persistence failure,
  and successful Save As/Save As New targets are recorded in Recent Files.
  Recent previously persisted only opaque IDs, which a fresh process could not
  resolve. It now persists private canonical paths, validates them at startup,
  and rebuilds new opaque IDs before any directory browse.
- The exact published Windows x64 package reproduces the reported CrossOver
  exit code 53. Its executable imports `z.dll`, but the ZIP does not contain
  it: vcpkg's application-local deployment populated the build tree while the
  install stage copied only the EXE. Windows packaging now copies that resolved
  DLL set before `windeployqt` and launches the staged executable with a
  system-only PATH before creating the ZIP.
- The published Linux AppImage checksum matches its sidecar and its version and
  X11 launch checks pass in a clean Ubuntu 22.04 container. Its Wayland launch
  aborts because the image has the platform entry points but omits their
  QtWayland libraries and runtime shell-integration plugins; the former gate
  inherited builder Qt paths and masked this. Linux packaging now deploys the
  complete Wayland client plugin tree and dependency closure, rejects
  unresolved plugin ELFs, and removes builder Qt paths from every package
  launch audit.
- Native macOS passes the full 10/10 production gate and 10 additional repeated
  XBAND runs. Its fresh ad-hoc-signed DMG passes version, signature, content,
  and disk-image audits with SHA-256
  `389b87d9ad68c302197fb1739ed74040122bee6dc06fa9c7111be4bd6e4574f9`.
  From that mounted DMG, a mouse Save created
  `/Users/jackmangano/fw-dmg-recent-0729.odt`; it was verified as a valid
  OpenDocument ZIP, the app was fully quit and relaunched, Recent listed the
  file without a folder browse, and Enter reopened it. The test file was moved
  recoverably to Trash.
- The final Ubuntu 22.04 container build passes 10/10. Its fresh AppImage passes
  clean-environment version, every-plugin `ldd`, X11, Wayland, and content
  audits with SHA-256
  `53f45ba5f1cefb2bcdedebbd8d0ca6ca477ca6bb6a03c295de3498073b9f16d3`.
  The ROM generator, Bash builder, PowerShell builder, and diff checks also
  pass.
- The published `v0.1.0` assets have not changed and remain failed tester
  previews. The macOS packaged Save/relaunch/Recent gate now passes. Remaining
  release gates are a native Windows rebuild plus real Windows first launch and
  a human Linux GUI pass.
- Next concrete action: the user is testing the `8993e37e…` DMG by hand -- Open
  from a lower browser row, the style combinations, and centre/right alignment
  including a wrapped paragraph. Then rebuild the AppImage at this revision and
  run it through the same checklist in a Linux GUI, and run the native Windows
  package job and verify its staged ZIP launches on real Windows. Windows has no
  post-fix package at all yet.

### 2026-07-29 — three-platform release production

Invariant: a public release may use only versioned packages built from one
public commit after each platform's real production and package audit passes.

- The existing Linux and macOS jobs previously validated builds but discarded
  their packages. They now retain the AppImage/DMG and SHA-256 sidecars as
  GitHub Actions artifacts.
- A native `windows-2022` job installs Qt 6.8.3 for MSVC 2022 x64, prepares the
  same pinned `snesrecomp`, and runs `packaging/windows/build-windows.ps1`.
  That script owns the complete build, 10-test production gate, Qt deployment,
  version/content audit, ZIP, and checksum.
- The first native configure exposed that the shared non-Apple install block
  made Windows require Unix Gettext/AppStream tooling and would have staged the
  executable under `bin/` while the Windows package contract requires it at the
  ZIP root. Platform install ownership is now explicit: Windows installs only
  the root executable for `windeployqt`; Unix alone owns desktop files, icons,
  manpages, translations, and AppStream metadata.
- The next native compile exposed one hidden transitive include:
  `tests/snes_machine_tests.cpp` used `std::string`/`std::to_string` without
  including `<string>`. MSVC rejected it correctly; the test now owns its direct
  standard-library dependency.
- The first complete native Windows test pass exposed two portable-semantics
  assumptions. `QDir::Hidden` follows the Windows hidden attribute and does not
  treat a leading dot as hidden, so the catalog now explicitly suppresses
  dot-prefixed names unless Show Hidden is enabled while retaining Qt's native
  hidden-attribute filter. The process test also required Markdown to preserve a
  rich-text bold property that its authoritative UTF-8 source cannot represent;
  ODT/DOCX/RTF still must round-trip bold, while Markdown must round-trip its
  exact source and rendered text.
- With all 10 native Windows tests green, staging and `windeployqt` completed.
  The package audit then exposed that PowerShell does not synchronously capture
  stdout from a GUI-subsystem executable, leaving the otherwise valid
  `--version` result empty. Windows now embeds standard `VERSIONINFO` product
  metadata from the CMake project version, and the package gate reads that
  native metadata instead of depending on console attachment behavior.
- PR 4 squash-merged as public commit `440ddef`. Its post-merge run
  `30483999477` passed all three production package jobs and retained exactly
  one versioned package plus checksum per platform.
- `v0.1.0` is published as an experimental GitHub prerelease with the audited
  macOS arm64 DMG, Linux x86_64 AppImage, Windows x64 ZIP, and their three
  checksum sidecars. GitHub's asset digests match the independently downloaded
  packages: DMG `624174003d53c735d2b55a7b8b4f99a6637263d65d9ea7957bc92a62151d6b01`,
  AppImage `f59e9a3beea0121d9b00e89cff3508508bcb6299e71c01560b8bb85368bdea93`,
  and ZIP `a53826956fd4b62709f62815711749e923461b31fb6e2e0f995e84a00c75a166`.
- This is automated build/package evidence, not physical Windows first launch,
  notarization, code signing identity, or human keyboard/mouse/dialog/recovery
  acceptance.

### 2026-07-29 — deterministic versioning after v0.1.0

Invariant: one committed value must determine the product version everywhere;
repository tags or dirtiness must never make identical source trees produce
different application versions.

- `VERSION` is the single `MAJOR.MINOR.PATCH` authority for CMake, executable
  metadata, all three package builders, package filenames, and CI artifact
  names.
- The legacy version compile helper no longer uses `git describe`. Public tags
  identify immutable releases; commit IDs and Actions run IDs identify
  development builds.
- `VERSIONING.md` defines pre-1.0 SemVer choices, exact-commit package
  publication, changelog/AppStream updates, immutable tags/assets, and rendered
  release verification. `v0.1.0` is the baseline.
- The manpage and tester guide no longer contain independently maintained
  current-package version strings.
- Local validation after the public tag exists proves the same source still
  reports `FairyWriter 0.1.0` rather than a `git describe` suffix. Native macOS
  and containerized Linux production package gates each pass all 10 tests,
  their version/content/display audits, and emit the `VERSION`-derived DMG or
  AppImage plus checksum. The workflow passes actionlint 1.7.12, both shell
  builders parse, the PowerShell builder parses, and the full branch delta
  passes Gitleaks.
- PR run `30485226084` passes the complete macOS arm64, Linux x86_64, and
  Windows x64 package jobs. In particular, native Windows proves that the
  canonical value reaches MSVC product metadata, the audited ZIP filename, its
  checksum sidecar, and the dynamically named retained artifact.

### 2026-07-29 — persistence/ROM reconciliation

Invariant: only public history reaches GitHub, resolved recovery history must
not masquerade as unfinished work, and the generated cartridge must obey real
SNES loader conventions rather than the vendored emulator's leniencies.

- The two agent commits were transplanted from the already-merged showcase tip
  onto current `origin/main`. Its tree matched the squash-merged public commit
  byte-for-byte, so the new branch contains no private-history merge base and
  needs no force push.
- Recovery format version 2 records an explicit resolved-transition bit.
  Successful primary saves and explicit Discard decisions retain every older
  generation in Recovery History but remove them from startup candidates. A
  later checkpoint is the sole unresolved candidate. Version-1 records remain
  readable and conservatively unresolved.
- The cartridge is now a canonical 64 KiB, two-bank LoROM. Bank-aware address
  helpers own program/data translation, the checksum is canonical, non-RESET
  vectors land on RTI, DB is explicit at reset, the advertised v3 header exists,
  and rival header-scoring windows cannot outscore the real `$7fc0` header.
- `fairywriter_cartridge_conformance` raises the production gate to 10 tests.
  The macOS CI label, active status, historical tile-capacity notes, changelog,
  and durable handoff now agree with that combined state.

### 2026-07-29 — durable persistence and Markdown

Invariant: viewport activity cannot make a document dirty, and no Save, Save
As, recovery restore, autosave, close, or document replacement can complete
without a durable typed result for one immutable content generation.

- `DocumentEngine` now has separate viewport revisions, content generations,
  content hashes, format identity, immutable snapshots, and full
  size/mtime/SHA-256 file fingerprints. Cursor movement, selection, Find, and
  scrolling do not advance content or create recovery.
- `DocumentPersistence` is the single production coordinator. It captures the
  live document on the UI thread and submits all filesystem work to one ordered
  worker. Current-file and confirmed-overwrite saves hash before encoding and
  again immediately before commit.
- New files use same-directory staging, complete write + file sync, a no-replace
  rename, and directory sync. Existing files use `QSaveFile` with direct-write
  fallback disabled. Corrupt/unsupported loads parse into a temporary document
  before active state changes.
- Checksummed, independent `.fwrecover` generations include document identity,
  path/format, canonical rich state or Markdown source, cursor/anchor, sequence,
  content hash, timestamp, and primary fingerprint. Rotation, adjacent
  deduplication, A-B-A history, pinned manual checkpoints, corrupt-newest
  fallback, matching-primary history, and automatic-primary conflict shutdown
  are implemented.
- F3 owns Save + Recovery / Recovery Only, 1-255 minute intervals, 0-255
  retained copies, Recovery History, and Markdown Rendered/Source. Close, New,
  Open, Recent, session replacement, and recovery share the cartridge-owned
  Checkpoint/Save/Discard/Cancel transition.
- First Save enters the ODT-default cartridge format/location flow. Save As
  offers ODT/DOCX/RTF/Markdown and enforces `.md`; `.markdown` opens as
  Markdown. The pinned cmark-gfm tag `0.29.0.gfm.13`, commit
  `587a12bb54d95ac37241377e6ddc93ea0e45439b`, is vendored for offline builds.
- Persistence contributes two tests; ROM conformance brings production to 10.
  The process test launches the actual production
  executable, types and formats through the cartridge input path, saves and
  relaunches ODT/DOCX/RTF/Markdown, edits Markdown in Rendered and Source mode,
  exits uncleanly after a checkpoint, restores through the cartridge, saves,
  and verifies the result from a third process.
- The real process test exposed and fixed two transport bugs that isolated
  component tests had missed: the host was replaying acknowledged event bytes,
  and the cartridge event cursor's high byte overlapped the retained-copy
  setting. Event acknowledgements now retire only complete host-owned records,
  the byte ring reserves one byte so full and empty cannot alias, and the guest
  cursor uses dedicated scratch at `$036b/$036c`.

### 2026-07-28 — three-platform tester readiness

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
  metadata, and executable icon are portable. The subsequent native Windows CI
  gate closes the build/test/deploy/package evidence gap but not physical first
  launch.
- Production registers 10 tests. The three retired presentation/theme tests
  remain development-UI-only and outside the production gate.
- Linux packaging is now `packaging/linux/build-linux.sh` and uses official Qt
  6.8.3 binaries on the Ubuntu 22.04/glibc baseline. macOS packaging stages the
  pruned arm64 bundle, deploys Qt, ad-hoc signs it, audits it, and makes a DMG.
  Windows packaging targets Qt 6.8/vcpkg/windeployqt. Every platform build
  output includes `COPYING` and `TESTING.md`; the Windows ZIP and macOS DMG
  contain them directly.
- The former long-document ODT fixture had once been restored after accidental
  framebuffer overwrite. Before public development it was removed because it
  contained private writing; the generated public ODT now preserves the real
  reader/edit/save/reopen coverage without publishing that content.

Current automated evidence: the sanitized public tree and current branch delta
pass Gitleaks and contain no loose ROMs, private documents, or packaged
applications in Git. The one public capture is the explicitly authorized,
silent H.264 MP4 with SHA-256
`1e7f9af86167cdce30afc5a7a023ab250f5c3ef9092f5b6286e60990b31f0024`.
Post-merge release run `30483999477` passed the 10-test and package gate on
macOS arm64, Linux x86_64, and Windows x64. Versioning run `30485226084` repeats
all three native jobs with artifact names and package metadata read from
`VERSION`.

Still manual/unproved: physical Windows first launch and SmartScreen behavior,
macOS Intel, signing/notarization identities, and the full real-fixture
mouse/keyboard/recovery pass on each platform. Do not launch the input-capturing
GUI unattended to close that gap.

Next concrete action after the deterministic-versioning publication boundary:
perform the complete human keyboard/mouse/recovery acceptance checklist on
actual target systems. Keep public `main` free of private history, ROMs,
documents, unauthorized captures, and packaged applications.

### 2026-07-28 — blue document surface

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

### 2026-07-28 — document panel encloses its plane

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

### 2026-07-28 — draggable scrollbar

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

### 2026-07-28 — pointer reach and optimistic-frame accuracy

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

### 2026-07-28 — wrap-aware vertical movement

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
to fit the shared resolve subroutine and vertical movement. The later help-plane
shift filled all 554 PPU tile slots in the historical 32 KiB layout. The current
64 KiB layout has 889 cartridge slots before its rival-header reservation, with
VRAM still imposing the real 1024-tile ceiling.

### 2026-07-27 — editing correctness

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
### Earlier development pass (undated)

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
