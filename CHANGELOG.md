# Changelog

## Unreleased

## 0.1.0 — tester preview

- Added native macOS arm64, Linux x86_64, and Windows x64 release jobs. Every
  job runs the production test and package audit before retaining a versioned
  package and SHA-256 sidecar for release publication.
- Kept resolved recovery generations as history without presenting them as
  unfinished startup work. A successful primary save or explicit Discard now
  closes the startup-prompt boundary for all older generations, while a later
  checkpoint becomes the sole unresolved candidate.
- Grew the cartridge from 32 KiB to 64 KiB LoROM, splitting it into a program
  bank and a data bank. Program space went from 59 free bytes to 21480, and the
  PPU tile budget from 0 free bytes to 10736 (554 of 889 slots used, against a
  1024-tile VRAM ceiling). LoROM was never the constraint — it addresses up to
  4 MB — and HiROM was rejected because it would relocate the SRAM mailbox off
  `$70:0000` to buy 64 KiB-contiguous data the VRAM ceiling makes unusable.
- Fixed the cartridge header checksum, which was computed with both the checksum
  and its complement zeroed rather than `$0000`/`$ffff`, leaving it `0x1fe` below
  the canonical value. Lenient loaders accepted it; a real verifier would not.
- Pointed every interrupt vector except RESET at an RTI landing pad instead of at
  the reset entry, wrote the v3 extended header that developer ID `$33` had been
  advertising without populating, and established DB explicitly at reset.
- Added `tools/fairywriter-romcheck` and the `fairywriter_cartridge_conformance`
  test, which check the canonical checksum, map mode against header position,
  vector resolution, and that no rival header location can outscore the real one
  and flip the image to HiROM.
- Opened the source tree for public development with clean project
  documentation, contribution guidance, issue templates, and Linux CI.
- Made the patched `snesrecomp` dependency reproducible from a fresh clone.
- Replaced the private long-document ODT test input with a deterministic,
  source-generated public regression fixture.

- Added the source-generated 32 KiB FairyWriter SNES cartridge and embedded
  production host.
- Added the SRAM command/event/viewport protocol and native document engine.
- Added cartridge-owned editor, browser, menu, help, formatting, proofing,
  keyboard, mouse, recovery, and scrolling behavior.
- Added macOS arm64 and Linux x86_64 packaging gates and Windows x64 packaging
  source.

This version is an engineering/tester milestone, not a stable end-user release.
