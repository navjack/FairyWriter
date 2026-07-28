# Changelog

## Unreleased

- Opened the source tree for public development with clean project
  documentation, contribution guidance, issue templates, and Linux CI.
- Made the patched `snesrecomp` dependency reproducible from a fresh clone.
- Replaced the private long-document ODT test input with a deterministic,
  source-generated public regression fixture.

## 0.1.0 — tester baseline

- Added the source-generated 32 KiB FairyWriter SNES cartridge and embedded
  production host.
- Added the SRAM command/event/viewport protocol and native document engine.
- Added cartridge-owned editor, browser, menu, help, formatting, proofing,
  keyboard, mouse, recovery, and scrolling behavior.
- Added macOS arm64 and Linux x86_64 packaging gates and Windows x64 packaging
  source.

This version is an engineering/tester milestone, not a stable end-user release.
