# FairyWriter Forward Plan (Synced 2026-07-28)

## Goal

Make the cartridge-owned FairyWriter product reproducible and suitable for
outside testers on macOS arm64, Linux x86_64, and Windows x64 without weakening
the existing document/runtime behavior.

## Product scope and invariants

- The native host runs an embedded, source-generated SNES cartridge.
- The cartridge owns the complete visible 256x224 UI, including help.
- `DocumentEngine` and the bridge own document/file/recovery state.
- One resident cartridge glyph set is shipped; there is no host-font or
  Unicode-glyph-streaming product path.
- Rich/proofing scope remains bold, italic, underline, spelling, and grammar.
- A tester build must be produced from the current source, pass the production
  regression gate, exclude loose ROM/scratch data, and carry `COPYING` plus
  `TESTING.md`.

## Current baseline

- The full macOS arm64 production configuration is green: ROM generator tests,
  focused XBAND end-to-end, and `ctest` **7/7**. The three retired Qt
  presentation tests exist only when `FAIRYWRITER_DEVELOPMENT_UI=ON`, where the
  expected total remains 10.
- A deterministic public ODT fixture exercises real import, chapter inference,
  boundary edits, undo/redo, and save/reload without publishing private
  writing.
- Iconv is gone. CP1252 is decoded by a small built-in table and unsupported RTF
  codepages fall back deterministically to Latin-1.
- Production host behavior is tester-safe: debug logging is opt-in, the window
  no longer targets Sidecar displays or steals focus back, fatal startup/runtime
  errors are visible, close writes recovery synchronously, and one optional
  document path is accepted.
- F2 opens a cartridge-owned 30x8 help plane. F1 or Backspace restores the exact
  originating mode and preserves menu selection.
- Windows source support is present: MSVC configuration is enabled, production
  and developer targets are separated, paths/durable writes are portable,
  application identity is explicit, the executable has a Windows icon, and a
  vcpkg/windeployqt ZIP builder emits a SHA-256.
- Linux targets official Qt 6.8.3 on the Ubuntu 22.04/glibc baseline; macOS
  continues to use the installed arm64 Homebrew Qt. Windows targets Qt 6.8.
- `TESTING.md` covers unsigned first launch, controls, recovery, the real-fixture
  acceptance pass, diagnostic logging, and known issues.
- Fresh macOS arm64 and Linux x86_64 artifacts pass their complete automated
  packaging gates. The top-level development app is synchronized from the same
  green macOS build. Windows remains source- and script-ready but unbuilt here.

The Phase 1-5 caret, proofing, 30x17 clear, panel, and draggable-scrollbar work
is part of the public source baseline.

## Ordered work

1. ~~Remove iconv and preserve common RTF behavior.~~ Complete and covered by
   CP1252 plus unknown-codepage real-RTF tests.
2. ~~Apply host-side tester polish and align production tests.~~ Complete;
   macOS production gate is 7/7.
3. ~~Add cartridge-owned help and its real-machine regressions.~~ Complete.
4. Complete platform builds:
   - ~~macOS arm64: rebuild, deploy only required Qt components, prune, ad-hoc
     sign, audit, create DMG and checksum.~~ Complete.
   - ~~Linux x86_64: build/test/package in the Qt 6.8.3 container, then pass
     AppImage version, X11, Wayland, and ROM/scratch audits.~~ Complete.
   - Windows x64: run the checked-in PowerShell builder on a Windows host,
     execute 7/7 tests, and perform the first-run checks in `TESTING.md`.
5. Run human acceptance on each actual platform with a substantial non-private
   ODT: edit across wraps, move vertically, drag/click the scrollbar,
   open/close Help, save, close with unsaved work, restore recovery, and confirm
   default logging is absent.
6. Publish the sanitized source baseline and public-development materials to
   `navjack/FairyWriter`.

## Non-negotiable automated gates

Run after every meaningful cartridge/frontend change:

- `GO111MODULE=off go test ./tools/fairywriter-rom`
- `ctest --test-dir build-arm64 -R fairywriter_xband_end_to_end --output-on-failure -VV`
- `ctest --test-dir build-arm64 --output-on-failure`

Production expectation is **7/7** on all platforms. Development UI expectation
is **10/10** when explicitly enabled.

## Manual boundaries

- This macOS host cannot prove Windows build/runtime behavior.
- The installed macOS Qt is arm64-only, so no Intel or universal artifact is
  claimed.
- Automated framebuffer, container, X11/Wayland launch, signing, and bundle
  audits do not replace the real mouse/keyboard/recovery acceptance pass.
- Do not launch the input-capturing GUI during unattended validation.

## Deferred scope

- New toolbar list/alignment features.
- Broader script/codepage rendering beyond the resident cartridge glyph set.
- Certificate signing, installer creation, and macOS Intel.
