# Recording the showcase video

`docs/media/fairywriter-showcase.mp4` is generated, not screen-captured. The
application records it itself: a scripted child mode drives the same
`RecompPlayer` a normal session runs — real cartridge, real SRAM mailbox, real
document engine — one emulated frame at a time, and writes the cartridge's own
framebuffer and the S-DSP's own output to two raw streams.

Nothing is re-timed afterwards. One emulated frame is one video frame and one
DSP audio block, so sixty video frames and 48000 audio samples are both exactly
one second and the two streams cannot drift apart. The consequence worth having
is that the video is deterministic: the same commit produces the same recording,
so refreshing the showcase for a release is a re-run rather than a re-shoot.

## Regenerating it

The child mode is compiled only when testing is enabled, so configure with
`-DBUILD_TESTING=ON` (the default in [BUILDING.md](../BUILDING.md)).

```sh
cmake --build build --parallel
./build/FairyWriter.app/Contents/MacOS/FairyWriter \
    --showcase-record build/showcase/frames.bgra build/showcase/audio.pcm
```

On Linux and Windows the executable is `build/fairywriter` and
`build\fairywriter.exe`; the arguments are the same. The run takes about three
seconds and prints the frame count, duration, sample rate, and two health
counters:

```text
frames=3798 seconds=63.30 rate=48000 silent=71 maxblocks=2
```

`silent` is frames the DSP had no block ready for, which are written as silence
rather than skipped — a couple of dozen at startup while the SPC700 driver is
still being uploaded is normal. `maxblocks` is the deepest the DSP's block queue
got; anything much above 2 means the recorder has fallen behind the guest and
the audio is lagging the picture.

Then mux. The raw video is about 220 MB per thousand frames and is disposable:

```sh
ffmpeg -y \
    -f rawvideo -pixel_format bgra -video_size 256x224 -framerate 60 \
        -i build/showcase/frames.bgra \
    -f s16le -ar 48000 -ac 2 -i build/showcase/audio.pcm \
    -vf "scale=1024:896:flags=neighbor" -pix_fmt yuv420p \
    -c:v libx264 -profile:v high -preset slow -crf 20 \
    -c:a aac -b:a 128k -ar 48000 -movflags +faststart \
    docs/media/fairywriter-showcase.mp4
```

`flags=neighbor` is not optional. Any interpolating scaler turns the cartridge's
glyphs into mush; the 4x integer upscale exists so the pixels survive H.264 and
the viewer's own scaling.

## Changing what it shows

The storyboard is `showcasePerform` in
[`src/snesrecomp_player_main.cpp`](../src/snesrecomp_player_main.cpp). Each act
is one thing the README claims FairyWriter does, performed through the input
paths a person would use: keystrokes enter through the XBAND scan table, and the
pointer moves in bounded relative packets from the cartridge's own start
position, so the toolbar and the shaper's faders are hit-tested by the guest
exactly as they are for a real mouse.

Two constraints the script has to respect, both learned by getting them wrong:

- A style toggle applies to a selection and only to a selection
  (`DocumentEngine` returns false without one). The demo therefore types a word,
  selects it back, and clicks the toolbar — it cannot arm a style and type into
  it. The styles then stack on their own, because each word inherits the format
  the previous one ended in, and that stacking is the thing worth showing.
  Anything added afterwards that re-toggles a style across the whole line will
  silently undo it.
- The recorder resets the sound and persistence settings to their defaults
  before it starts. The shaper writes every edit through to `settings.ini`, so
  without that reset the second recording would open on the voice the first one
  left behind.

The recorder also runs under its own application name and a scratch catalog
root. That keeps it off the developer's real saved settings, and keeps real
filenames from the home directory out of a public video.

## Publishing it

GitHub only renders a video player for assets it hosts itself, so the README's
inline player is a `user-attachments` URL rather than the file in this
repository. That upload has no API: to refresh the player, drag
`docs/media/fairywriter-showcase.mp4` into any GitHub comment box, let it
upload, copy the `https://github.com/user-attachments/assets/...` URL it
produces, and replace the bare URL line in the README's Showcase section.
Discard the draft comment afterwards — the asset stays.
