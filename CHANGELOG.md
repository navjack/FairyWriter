# Changelog

## Unreleased

- The retired FocusWriter-derived Qt desktop UI has been removed from the source
  tree. It had not been part of the shipping product for some time — it built
  only behind an option that defaulted off, and a bundle-prune step existed
  purely to strip its resources back out before packaging — but it was still
  three quarters of `src/`, and it carried theme artwork, icon sets, a sound set,
  a symbol table, a font, the translation catalogues, and the spell-checking and
  single-instance dependencies with it. Nothing users see changes: the built
  application bundle is identical, file for file, before and after. What changes
  is that there is now one build configuration instead of two, and every platform
  compiles only code the cartridge product actually runs.
- Typing no longer makes the page flicker. Every keystroke used to draw one frame
  with all styling thrown away — bold, italic and underline flattened, spelling
  and grammar marks gone, the selection highlight blanked — before the host put it
  back a couple of frames later. The cartridge now keeps that state through its
  own local frame and moves it with the text it edits, so what you see while
  typing is what the document already is.
- Delete removes the character under the cursor. It was shifting a scratch region
  nothing reads, so for two frames the line dropped its *last* character instead,
  then snapped back.
- Selecting to the end of the visible text highlights it. The end of a selection
  was never recorded when it landed past the final character — which is exactly
  where Shift+End and select-all put it — so the highlight silently did not
  appear at all.
- Shift+arrow grows the highlight with the caret instead of trailing a character
  behind it.
- Smoother motion: the emulated machine is driven at its true 60.0988 Hz rather
  than a rounded 58.8, which removes a dropped or repeated frame roughly once a
  second.
- The showcase video is gone, and so is the machinery that recorded it. GitHub
  renders a video player only for assets it hosts itself, and creating one has no
  API, so the README's embedded player could not be refreshed by any automated
  step — and it had already drifted to a superseded silent recording of an older
  build. Keeping it current meant a manual upload before every release. The
  README's video, the committed MP4, and the self-recording mode that produced it
  have all been removed.

## 0.3.0 — 2026-07-30 — tester preview

- FairyWriter has sound. Typing now makes a blip, and it is a real one: the
  cartridge uploads an SPC700 driver to the audio processor through the boot
  ROM's own handshake, and every note is the S-DSP keying a voice. Nothing is
  synthesised on the host — the desktop side only carries the samples the
  emulated chip produced to an output device.
- An SPC700 sound shaper on **F5**, listed in the F2 help card. Twelve settings,
  each one a real S-DSP register field rather than a host-side approximation:
  blips on/off, waveform (square, triangle, or the DSP's hardware noise source),
  attack, decay, sustain level, sustain rate, release, pitch, volume, and the
  echo unit's volume, delay and feedback. Every numeric setting has a slider you
  can drag with the mouse or step with the arrow keys, bounded by the width of
  the hardware field it is written into. Settings persist across restarts.
- The shaper previews as you work: Space or Enter plays the voice, and every
  edit auditions itself, so nothing has to be tried out by returning to the
  document and typing.
- Two live scopes below the sliders, and neither is an illustration. The SPC700
  captures the DSP's own OUTX and ENVX registers into audio RAM at its own
  clock -- once per video frame would be far too coarse for a 2 kHz waveform or
  a 60 ms envelope -- and the cartridge streams those samples back through the
  mailbox ports. The upper trace is the voice's actual output; the lower one is
  its actual envelope.
- The envelope has a release stage at all. A blip is one-shot and nothing ever
  sent a key-off, so the voice ran attack, decay and sustain-fade and the GAIN
  release rate was never read -- the RELEASE control did nothing. The driver now
  performs the hardware's own note-off, clearing ADSR1 bit 7 mid-note so the
  envelope hands over to GAIN.
- The default blip is a 62 ms square wave — long enough to read as a pitch
  rather than a click, and finished before the next keystroke even at twenty
  characters a second.
- Square and triangle are BRR waveforms encoded from source at build time. No
  sample data is taken from any commercial ROM.
- The echo unit writes to audio RAM on its own, so the cartridge reserves a
  buffer for it and installs the buffer address before enabling the writes.

## 0.2.0 — 2026-07-29 — tester preview

- Bold, italic and underline are now independent on screen. The cartridge had
  four glyph pages and picked one shape by priority, so underline hid bold and
  bold hid italic; holding two of them showed only one. There is now a glyph
  page per combination — the style bits are the page index, so the draw loop
  masks them instead of resolving a priority — and every combination renders as
  itself. Underline also moved to the palette's pale blue and stops a column
  short of the cell edge, so it reads as a line under its own text instead of a
  bar against the next line.
- Paragraph alignment is rendered. Left, centre and right now place each visual
  line on its own width, so a wrapped centred paragraph is centred line by line
  and the space a line broke at no longer counts toward its width. The caret
  follows a moved line, and clicking or arrowing into one lands on the character
  under the pointer. Justify renders as left.
- Fixed opening a document from any browser row but the first. The cartridge
  indexed the per-row flags and opaque-ID-length tables, which hold one byte per
  visible row, with the 32-byte stride of the ID table, so Enter on a lower row
  read another row's bytes and emitted an empty or truncated ID. Because folders
  sort ahead of documents, that is nearly every real Open: the host could not
  resolve the ID and the browser returned to the unchanged document. Selecting a
  folder below the first row failed the same way, and mouse clicks share the
  path. An ID the catalog cannot resolve now also reaches the cartridge as a
  visible open failure instead of vanishing.
- Give a replacement document a fresh view. Opening a file, restoring recovery,
  or switching sessions no longer forces the previous document's scrollbar
  anchor onto the new one, which could open a document at its end.
- Fixed first Save/Save As so the visible browser folder is the sole save
  destination, returning to a parent cannot retain a stale child folder, and
  the cartridge explains when to press N to create the new file.
- Replaced the filename overlay with a complete cartridge-owned Save dialog:
  the old browser rows are cleared, the title and formatting cards become
  filename context plus clickable Save/Cancel buttons, and Tab/arrows visibly
  move one shared keyboard/mouse focus ring. Sub-frame desktop clicks are
  retained until the Super NES Mouse report is latched instead of being lost
  when press and release arrive between guest frames.
- Reject stale catalog parent IDs instead of silently treating them as the
  catalog root, report invalid/read-only save targets in the cartridge, and add
  successful Save As targets to Recent Files. Persist host-private canonical
  paths so a fresh process can rebuild its opaque catalog IDs and open Recent
  immediately after relaunch.
- Make the Windows package carry vcpkg's application-local runtime DLLs and
  launch its staged executable with a system-only PATH before creating the ZIP.
- Deploy the complete Qt Wayland client plugin/runtime closure in the Linux
  AppImage and audit it without the build image's Qt environment.

## 0.1.0 — 2026-07-29 — tester preview

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
