# FairyWriter 0.1 tester guide

FairyWriter is a native desktop application running a source-generated SNES
word-processor cartridge. The 256x224 interface, menus, help, caret, selection,
proofing marks, and formatting toolbar all come from that cartridge. No
commercial ROM is included.

This test release is deliberately unsigned. Verify the SHA-256 file beside the
download before opening it, and report both the artifact name and hash with any
bug.

## Install and first launch

### macOS (Apple Silicon)

1. Open `FairyWriter-0.1.0-macos-arm64.dmg`.
2. Copy `FairyWriter.app` to Applications.
3. Control-click or right-click the copied app, choose **Open**, then choose
   **Open** again. This is the normal first-launch path for an unsigned build.

If Gatekeeper still retains the download quarantine, run:

```sh
xattr -dr com.apple.quarantine /Applications/FairyWriter.app
```

This artifact is Apple-Silicon-only. Intel macOS has not been built or tested.

### Windows 10/11 x64

1. Extract `FairyWriter-0.1.0-windows-x64.zip` completely.
2. Run `fairywriter.exe` from the extracted folder.
3. If Microsoft Defender SmartScreen appears, choose **More info**, then
   **Run anyway**.

Do not move only the `.exe`; the Qt DLLs and plugin folders beside it are part
of the application. The first Win64 tester build still requires verification on
a real Windows x64 machine.

### Linux x86_64

Make the AppImage executable and run it:

```sh
chmod +x FairyWriter-0.1.0-x86_64.AppImage
./FairyWriter-0.1.0-x86_64.AppImage
```

If FUSE is unavailable, AppImage's extraction fallback may be used:

```sh
./FairyWriter-0.1.0-x86_64.AppImage --appimage-extract-and-run
```

## Controls

The in-app reference is always available with **F2**. **F1** or **Backspace**
returns to the screen you came from.

### Writing and navigation

- Type normally; Shift and Caps Lock control letter case.
- Arrow keys move the caret. Shift+Arrow extends the selection.
- Home, End, Page Up, Page Down, Delete, Backspace, and Enter have their usual
  editing meanings.
- F1 or Escape opens/closes the cartridge menu.
- F2 opens/closes Help.
- F3 opens Save and Recovery settings.
- F4 opens Find.

On macOS, either Command or Control works for the shortcuts below. On Linux and
Windows, use Control.

- Ctrl/Command+N: new document
- Ctrl/Command+S: save
- Ctrl/Command+Z / Y: undo / redo
- Ctrl/Command+B / I / U: bold / italic / underline
- Ctrl/Command+A: select all
- Ctrl/Command+C / X / V: copy / cut / paste
- Ctrl/Command+F: find the next occurrence of the clipboard text
- Ctrl/Command+[ / ]: decrease / increase paragraph indent
- Ctrl/Command+Shift+L / E / R: align left / center / right

### Mouse

- Click in the document to place the caret; drag to select.
- Click `[B]`, `[I]`, `[U]`, `[L]`, `[C]`, or `[R]` in the upper-right toolbar
  to change formatting.
- Click or drag anywhere on the thin position bar above the document to scroll.
  Scrolling moves the view while keeping the caret in place; click in the
  scrolled text to place the caret there.

## Required acceptance pass

Open a substantial, non-private ODT with at least two chapters, or generate the
public development fixture with:

```sh
GO111MODULE=off go run ./tools/public-test-fixture \
  "/tmp/FairyWriter Public Regression.odt"
```

Pass that path to FairyWriter when launching, then check:

1. Type through a wrap boundary and move Up/Down through the visible wrapped
   lines. The caret should keep its intended column across a short line.
2. Shift+Arrow in the middle of the document. The blinking caret should stay at
   the active edge of the selection.
3. Click in both the upper and lower document rows. The caret should land where
   clicked.
4. Drag the position bar from start to end, then click in the scrolled view.
   The text should move, the thumb should follow, and no false caret should
   appear while the real caret is outside the window.
5. Move from a full viewport to a short final viewport. No stale characters
   should remain in the lower rows.
6. Press F2, then return with both F1 and Backspace. Help should return to the
   exact document/menu/browser screen it came from.
7. Toggle bold, italic, underline, and alignment with both keyboard and mouse.
8. Create a new document and save it. First Save must open **SAVE AS FORMAT**
   with ODT selected, then the cartridge file browser. Save without typing an
   extension and confirm FairyWriter creates a non-empty `.odt`.
9. Close FairyWriter normally, launch a fresh process, open that ODT, and verify
   both text and formatting. Repeat Save As/relaunch for DOCX, RTF, and Markdown;
   Markdown must use `.md`.
10. In a Markdown document, use F3 to switch between Rendered and Source. Raw
    HTML, destinations, and front matter must remain inert; no link, script,
    style, local image, or network image may execute or load.
11. Make an unsaved edit, choose Close, and exercise the cartridge-owned
    **CHECKPOINT / SAVE / DISCARD / CANCEL** screen. Cancel must return to the
    document. Checkpoint must allow the transition only after recovery is
    durable.
12. Relaunch after a checkpoint or forced termination, accept
    **RECOVERY AVAILABLE / ENTER RESTORES**, verify the restored document is
    dirty, save it, close, and verify it again from a third fresh process.
13. In F3 settings, verify Recovery Only and Save + Recovery, the 1–255 minute
    interval, the 0–255 retained-copy range, and Recovery History. A copy count
    of 0 must stop timed writes while explicit Save and Checkpoint still work.
14. Confirm no debug log is created during an ordinary run.

## Files and recovery

Documents stay wherever you open or save them. Recovery files and session state
use the operating system's per-application data directory:

- macOS: `~/Library/Application Support/FairyWriter`
- Windows: `%LOCALAPPDATA%\FairyWriter`
- Linux: `$XDG_DATA_HOME/FairyWriter`, or `~/.local/share/FairyWriter` when
  `XDG_DATA_HOME` is unset

New documents and first Save default to ODT. Save As offers ODT, DOCX, RTF, and
Markdown; existing FODT and plain-text documents remain load/save compatible.
Primary replacements and recovery generations are committed atomically. If a
filesystem cannot provide the required same-filesystem replacement semantics,
FairyWriter reports the failure and leaves the prior primary file intact.

Recent-file configuration uses:

- macOS: `~/Library/Preferences/FairyWriter`
- Windows: `%LOCALAPPDATA%\FairyWriter`
- Linux: `$XDG_CONFIG_HOME/FairyWriter`, or `~/.config/FairyWriter` when
  `XDG_CONFIG_HOME` is unset

## Reporting a bug

Please include:

- operating system version, CPU architecture, artifact filename, and SHA-256;
- exact steps and what you expected instead;
- whether it reproduces after relaunch;
- a screenshot or short recording for visual/input defects;
- a small affected document when it is safe to share.

For an input/runtime trace, enable the opt-in debug log before launching:

macOS:

```sh
FAIRYWRITER_DEBUG_LOG=1 /Applications/FairyWriter.app/Contents/MacOS/FairyWriter
```

Linux:

```sh
FAIRYWRITER_DEBUG_LOG=1 ./FairyWriter-0.1.0-x86_64.AppImage
```

Windows PowerShell:

```powershell
$env:FAIRYWRITER_DEBUG_LOG = "1"
.\fairywriter.exe
```

The trace is named `fairywriter-keypath.log` in the platform temporary
directory (`$TMPDIR` on macOS, `/tmp` or `$TMPDIR` on Linux, `%TEMP%` on
Windows). Debug logging is off by default.

## Known issues

- Left, Right, Home, and End still use 8-bit arithmetic for the cartridge's
  optimistic pre-host frame. In documents over 255 characters the caret can be
  wrong for one frame, then the authoritative host viewport corrects it.
- RTF imports have deterministic CP1252 support. Other unsupported legacy
  codepages fall back to Latin-1, preserving ASCII but not byte-accurate
  Shift-JIS, Big5, GB2312, or Mac codepage text.
- The resident cartridge font does not provide Unicode/emoji glyph streaming.
- The artifacts are unsigned, so macOS Gatekeeper and Windows SmartScreen show
  the first-launch warnings documented above.
- Win64 behavior still needs a real Windows machine pass. macOS Intel is not a
  supported artifact in this tester round.
