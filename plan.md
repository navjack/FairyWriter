# Typing blip sound: an SPC700/S-DSP audio path for FairyWriter

## Context

FairyWriter has no audio at all today. The request is a typing blip — an on/off
checkbox plus a dedicated menu for shaping the voice (attack, decay, sustain,
waveform: square / triangle / noise), with a default modeled on the per-character
blip in the Mega Man X intro's command-line text crawl.

The important discovery from exploration: **this is not a from-scratch audio
engine.** The SPC700 and S-DSP are already emulated and already compiled into the
production binary. `CMakeLists.txt:413-432` builds `fairywriter_snes_machine` from
`snes/apu.c`, `snes/spc.c`, `snes/dsp.c` and `snes/audio_shadow.c`, and
`src/snes_machine.c:3-6` already includes their headers. What is missing is only:

1. `clock_position()` in `src/snes_machine.c:70` never advances the APU — it
   clocks the PPU and DMA only, and never accumulates `snes->apuCatchupCycles`,
   so `snes_catchupApu()` (`../snesrecomp/runner/src/snes/snes.c:151`) always has
   zero cycles to run.
2. Nothing drains the DSP's output ring (`dsp_getSamples`, `dsp.c`), and there is
   no output device.
3. The cartridge never uploads an SPC700 program, so even a clocked APU would sit
   in its IPL boot loop. The real IPL boot ROM *is* present
   (`apu.c:16`, `bootRom[0x40]`), so the standard upload handshake works.

So the work is: clock the APU, drain it into a real device, write a small SPC700
driver and BRR waveforms into the cartridge, and wire a settings surface. Doing it
this way means Attack/Decay/Sustain map **literally** onto `VxADSR1`/`VxADSR2` and
noise is the hardware's own — nothing is simulated or invented.

## Hardware facts this design rests on

Verified against the SNESdev wiki S-DSP register reference:

- `VxADSR1 ($x5)` = `EDDD AAAA` — bit 7 ADSR enable, bits 6-4 decay rate (3 bits),
  bits 3-0 attack rate (4 bits).
- `VxADSR2 ($x6)` = `LLLR RRRR` — bits 7-5 sustain level (3 bits), bits 4-0
  sustain rate (5 bits).
- `GAIN ($x7)`: bit 7 clear = direct fixed envelope; bit 7 set = `1MMr rrrr`,
  mode in bits 6-5, rate in bits 4-0. This is where "release" lives.
- `FLG ($6C)` = `RMEN NNNN` — soft reset, mute, echo-write-disable, and the
  4-bit **noise frequency**. `NON ($3D)` switches a voice from BRR to noise.
- `PITCH ($x2/$x3)`: 14-bit, 2.12 fixed point. `VOL ($x0/$x1)` and
  `MVOL ($0C/$1C)` are signed 8-bit.
- `DIR ($5D)` is an ARAM page number; entry address = `DIR*0x100 + SRCN*4`, each
  entry four bytes: start pointer, then loop pointer.
- BRR block = 9 bytes: header `rrrrffle` (range, filter, loop, end) then 8 data
  bytes holding 16 four-bit samples.

The DSP has no oscillators, so **square and triangle are BRR samples** we encode
ourselves; only noise is generated in hardware.

### Do not extract Mega Man X sample data

The MMX reference is for *character*, not content. Its BRR data is Capcom's. We
generate our own square and triangle in Go and tune the envelope by ear against
the linked recomp build until the blip reads the same way — fast attack, short
decay, quick release, high pitch.

## Decisions taken

- **Output sink: vendored miniaudio** (single public-domain header in
  `third_party/`). Qt Multimedia's `QAudioSink` routes through the platform media
  backend plugin — on Linux the ffmpeg plugin — and `packaging/linux/build-linux.sh`
  has an explicit gate that fails on unresolved plugin dependencies. miniaudio
  wraps CoreAudio/ALSA+Pulse/WASAPI with zero packaging changes on any of the three
  platforms and matches the existing vendoring pattern (`third_party/cmark-gfm`,
  `third_party/snesrecomp`).
- **UI: a new cartridge plane**, reached by F5 and by a new row in the F1 main
  menu. The existing F3 plane is titled "SAVE AND RECOVERY" and is full (5
  selectable rows in 8 lines).
- **Full voice exposed**: waveform, attack, decay, sustain level, sustain rate,
  release, pitch, volume.

## Implementation

Five phases, each independently gateable.

### Phase 1 — Clock the APU and get silence out of a real device

`src/snes_machine.c`
- In `clock_position()`, accumulate `snes->apuCatchupCycles += apuCyclesPerMaster * 2`
  (the function advances `hPos` by 2 master cycles per call). `apuCyclesPerMaster`
  is `static const` inside `snes.c:23`; replicate the expression with a comment
  citing that line rather than patching the vendored checkout.
- Call `snes_catchupApu(machine->snes)` once per completed frame in
  `fairy_snes_run_frame()`. Port reads at `$2140-$2143` already catch up on their
  own (`snes.c:200-205`).
- Replace the `RtlApuLock`/`RtlApuUnlock` no-ops (`snes_machine.c:29-30`) with a
  real mutex — the audio callback runs on miniaudio's thread and the vendored core
  expects these hooks to serialize it against the CPU thread.

`src/snes_machine.h` — new C API:
- `int fairy_snes_audio_read(FairySnesMachine*, int16_t* out, int frames)` —
  returns 0 when fewer than 534 native samples are buffered, else calls
  `dsp_getSamples`.
- `uint8_t fairy_snes_debug_aram(const FairySnesMachine*, uint16_t)` and
  `uint8_t fairy_snes_debug_dsp_reg(const FairySnesMachine*, uint8_t)` — needed by
  the tests in later phases; add them here so the test harness grows once.

New `src/audio_output.h/.cpp` — miniaudio device plus a lock-free SPSC ring.
Producer is `advanceFrame()` (one block per emulated frame); consumer is the
miniaudio callback. Underrun outputs silence and never stalls the guest. **Device
init failure must be non-fatal** — the app runs silently.

`src/snesrecomp_player_main.cpp` — after `fairy_snes_run_frame()` in
`advanceFrame()` (~line 706), pull one block and push it to the ring. Output block
size is `deviceRate/60` samples per emulated frame; `dsp_getSamples` consumes
exactly 534 native samples per call and resamples, so the ring absorbs the drift
between the 17 ms `QTimer` (`snesrecomp_player_main.cpp:274`) and true 60 Hz.

`CMakeLists.txt` — vendor the header, add `src/audio_output.cpp` to the production
target, link the platform audio frameworks miniaudio needs. Add one
`THIRD_PARTY_NOTICES.md` entry.

**Gate:** full `ctest` green and `fairywriter_xband_end_to_end` ×10. This phase's
whole point is proving the added per-frame APU work does not disturb input
timing — the handoff records a prior case where a slower `clearStage` silently
dropped queued keystrokes.

### Phase 2 — SPC700 driver, BRR waveforms, and a hard-coded blip

All in `tools/fairywriter-rom/main.go`, in the existing hand-assembled style.

- New bank-0 constant `spcImageOffset = 0x7000` and a matching entry in `build()`'s
  region table (`main.go:5913-5935`), splitting `{"program", 0, len(program), scanMapOffset}`
  into `program → spcImageOffset` and `SPC image → scanMapOffset`. The program
  currently ends at `0x313e`, leaving ~16 KB of headroom, and the existing check
  enforces it. Bank 0 is the right home: bank 1 has only 1490 bytes free before
  `bank1Limit`, and the SPC image is read byte-at-a-time with `LDA.l`, so its bank
  is irrelevant.
- `brrEncode(samples []int16) []byte` — a real encoder in Go, so the waveforms are
  source-owned and testable. Emit one self-looping block per waveform (square,
  triangle) plus a 256-byte-aligned two-entry DIR page.
- SPC700 driver (~150-200 bytes). On entry: set `DIR`, `MVOL L/R`, `FLG = $20`
  (echo writes off, not muted, not reset), `EON = 0`, `PMON = 0`, `EVOL = 0`. Then
  poll port 0 (`$F4`) for exactly two commands, writing DSP registers through
  `$F2`/`$F3`:
  - **configure** — payload is the full voice parameter block (VOL L/R, PITCH L/H,
    SRCN, ADSR1, ADSR2, GAIN, NON bit).
  - **blip** — `KON = 1`.
  The SPC echoes port 0 so the 65816 can confirm the write landed.
- 65816 IPL upload in the init path (`emitProgram`, `main.go:690`+), after PPU
  setup and before NMI is enabled: wait for `$BB/$AA`, write the target address,
  `$CC`, then the per-byte counter handshake, then the entry-point jump. **Every
  wait must be bounded** by a counter that falls through to an "audio unavailable"
  flag — an unbounded spin would hang boot and every headless test.
- Trigger: in the printable-insert path (`insertPrintableCall`, `main.go:2312`),
  store the blip byte to `$2140` when the enable flag in the cartridge state block
  is set.

**Gate:** `GO111MODULE=off go test ./tools/fairywriter-rom`, then the handoff's
sequence — xband end-to-end, then full `ctest`.

### Phase 3 — Settings wire and persistence

- `src/document_persistence.h` — a `SoundSettings` struct beside `PersistenceSettings`
  (`document_persistence.h:56`) with the same `load(QSettings&)` / `save(QSettings&)`
  shape, reusing `persistenceSettings()` (`document_persistence.cpp:306`) so it
  lands in the existing `settings.ini`.
- `src/document_bridge.h` — `CommandGetSoundSettings = 0x0116`,
  `CommandSetSoundSettings = 0x0115`, `EventSoundSettings = 0x8214`, following the
  persistence-settings triple at lines 25-26 and 56.
- `src/document_bridge.cpp` — `publishSoundSettings()` modeled directly on
  `publishPersistenceSettings()` (line 317). Payload is nine bytes: enabled,
  waveform, attack, decay, sustain level, sustain rate, release, pitch, volume —
  one atomic record, so no field can tear, matching the reasoning already recorded
  for the three-byte persistence value.
- Cartridge side: hold the same nine bytes in the `$03xx` state block, project
  them into the SPC configure command on change, and re-emit the host command the
  way `settingsEmitConfig` already does (`main.go:3665`+).

### Phase 4 — The cartridge sound plane

- `soundPlane()` next to `settingsPlane()` (`main.go:284`), and a `soundOffset`
  page in bank 1 — 240 bytes out of the 1490 free, which shifts `tilesOffset` up by
  `0xf0` and leaves ~39 tiles of headroom against the 881-slot cap.
- New mode byte value at `$031d` (`$11` is taken; use `$12`), a `renderSound`
  routine modeled on `renderSettings` (`main.go:4840`) reusing its `emitByteDigits`
  helper for numeric fields, and a `soundInput` handler modeled on `settingsInput`
  (`main.go:3609`) for up/down/left/right/back.
- New key: F5 is PS/2 set-2 scancode `0x03`; map it to key code `0x1e` in
  `xbandScanMap()` (`main.go:650`). Codes `0x11-0x1d` are taken and `0x20`+ is the
  printable threshold, so `0x1e` is clean.
- Add a "SOUND..." row to `mainMenuPlane()` (`main.go:255`) and a line to
  `helpPlane()` (`main.go:271`).
- Both planes are 30 columns × 8 rows — the 9-row limit note at `main.go:249-254`
  applies.

### Phase 5 — Tune the default

Render headless: run N frames in `snes_machine_tests`, dump `fairy_snes_audio_read`
output to a WAV, listen, adjust. Target the MMX text-crawl character: square wave,
near-instant attack, short decay, low sustain level, fast release, high pitch. The
register field layout above is verified; the specific values are an ear judgment
and should not be guessed in advance.

## Files

| File | Change |
|---|---|
| `src/snes_machine.c` / `.h` | APU cycle accumulation, per-frame catch-up, real `RtlApuLock`, audio-read + ARAM/DSP debug accessors |
| `src/audio_output.h` / `.cpp` | New — miniaudio device and SPSC ring |
| `src/snesrecomp_player_main.cpp` | Produce one audio block per frame; sound-settings command handling |
| `tools/fairywriter-rom/main.go` | SPC700 driver, BRR encoder + waveforms, IPL upload, blip trigger, sound plane, F5, menu/help rows |
| `src/document_bridge.h` / `.cpp` | Sound-settings command/event triple and publisher |
| `src/document_persistence.h` / `.cpp` | `SoundSettings` + QSettings round-trip |
| `CMakeLists.txt`, `THIRD_PARTY_NOTICES.md` | Vendor miniaudio, add the new sources, notices entry |
| `tests/snes_machine_tests.cpp`, `tools/fairywriter-rom/main_test.go` | See below |

## Verification

Per-phase gates, in the order the handoff protocol requires:

```bash
GO111MODULE=off go test ./tools/fairywriter-rom
```

```bash
ctest --test-dir build-arm64 -R fairywriter_xband_end_to_end --output-on-failure -VV
```

```bash
ctest --test-dir build-arm64 --output-on-failure
```

New permanent coverage:

- `main_test.go` — BRR encode/decode round-trip within quantization error; SPC
  image fits its region and does not span a bank boundary; the sound plane is 30
  columns × 8 rows; F5's scancode does not collide.
- `tests/snes_machine_tests.cpp` — after N frames the driver signature is present
  in ARAM (`fairy_snes_debug_aram`); typing a printable character sets `KON` and
  produces non-silent samples; with blips disabled the output stays silent; a
  sound-settings command changes `ADSR1`/`ADSR2` in the DSP; and the bounded IPL
  upload still completes when the APU never answers (fallback flag set, no hang).
- Ring-buffer block math covered in the audio unit test.

Manual, on a real launch of the app: type in a document and confirm the blip fires
per character and does not lag typing; open the plane with F5, toggle blips off and
confirm silence; change waveform and each envelope field and confirm each is
audible; quit and relaunch to confirm the settings persisted.

## Risks

- **Frame-timing regression.** Clocking the APU adds work to every emulated frame,
  and `fairywriter_xband_end_to_end` is documented as sensitive to exactly this.
  Phase 1 exists to isolate it: it changes timing without changing behavior, so a
  regression there is unambiguous. Run it ×10 per the handoff protocol.
- **The IPL handshake must be bounded.** An unbounded wait hangs boot and every
  headless test. Every wait loop gets a counter and an "audio unavailable"
  fallback.
- **Bank 1 is tight** — 1490 bytes free. The sound plane takes 240 of it; the SPC
  image deliberately goes to bank 0 instead. Re-measure after Phase 4.
- **65816 branch range.** `main.go`'s `branch()` panics rather than silently
  wrapping (a defect this file has already had twice); new code in the dispatch
  path may need the documented inverted-condition trampoline (`main.go:715-723`).
- Only the ADSR path is exercised by the default preset; the GAIN release modes
  are a second code path and need their own test case.
