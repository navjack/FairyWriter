# Durable Save, Recovery, Autosave, and Markdown Support

## Summary

Refactor persistence around one invariant: a document transition is complete only after its contents and recovery state are durably committed and can be reconstructed by a fresh process.

ODT remains the default format. Save, Save As, loading, crash recovery, timed autosaving, and Markdown will use the same persistence coordinator and typed outcomes. The production test gate will add real process-level save/exit/relaunch coverage on macOS and Linux.

## Core Changes

- Split document state into:
  - viewport revision for cursor, selection, and mailbox updates;
  - content generation for text or formatting changes;
  - saved-content identity for accurate dirty detection.
- Cursor movement, selection, searching, and scrolling must never mark a document dirty or produce checkpoints.
- Introduce explicit types for `DocumentFormat`, immutable `DocumentSnapshot`, file fingerprints, persistence settings, and detailed persistence results instead of ambiguous booleans.
- Centralize load, Save, Save As, conflict detection, recovery, and autosave outside the player window.
- Capture immutable snapshots on the UI thread and perform disk work through one ordered persistence worker. Close and document-replacement operations wait for a durable result before continuing.

## Save, Load, and File Safety

- New documents start as ODT. First Save enters the same format-and-location flow as Save As instead of reporting a conflict.
- Save As first selects ODT, DOCX, RTF, or Markdown, defaulting to the current format and ODT for new documents. The cartridge appends the correct extension.
- Continue opening and saving existing FODT and plain-text documents for compatibility, without offering them as new-document choices.
- Save As New writes to a same-directory temporary file and atomically renames it only after a complete write and sync. A failed save must never leave an empty target.
- Existing-file replacement uses strict atomic commit with direct-write fallback disabled. If the filesystem cannot provide the required safety, fail visibly while preserving the original.
- Track size, modification time, and SHA-256. Recheck content immediately before commit so same-timestamp replacements and external edits are detected.
- Parse and validate a load completely before replacing the active document. Corrupt, unsupported, missing, or unreadable files leave the current document untouched.
- Avoid assumptions about path separators, case sensitivity, inode identity, or timestamp resolution. Use same-filesystem staging and capability-aware failure handling.
- Apply one cartridge-owned dirty-transition dialog to Close, New, Open, Recent, and Recovery:
  - **Checkpoint** writes a durable recovery state and continues the transition.
  - **Save** saves the primary document, invoking Save As when untitled.
  - **Discard** abandons current unsaved generations but retains older valid version history.
  - **Cancel** returns to the document.

## Autosave and Recovery

- Add cartridge settings, reachable with F3:
  - Mode: Recovery only or Save + Recovery.
  - Interval: 1–255 minutes; default 1 minute.
  - Retained copies: 0–255; default 5.
  - Recovery History.
  - Markdown Rendered/Source mode when applicable.
- A count of 0 disables all timed persistence. Explicit Save still works, and explicit Checkpoint creates one pinned recovery state.
- Start the elapsed-time deadline with the first content change after the last durable state. Continuous editing creates at most one unique generation per interval; idle documents cause no writes.
- Compare each candidate snapshot with the immediately preceding durable content hash. Formatting changes count; cursor-only changes do not. An A→B→A editing sequence remains valid history.
- For named documents in Save + Recovery mode:
  1. Commit the recovery snapshot.
  2. Save the identical snapshot to the primary file.
  3. Record whether the recovery generation matches the primary fingerprint.
- Store independent, checksummed `.fwrecover` snapshots in the application recovery directory. Each contains document identity, original path and format, canonical editor state, cursor/selection, sequence, wall-clock time, content hash, and primary-file fingerprint.
- Rotate only after a new generation commits successfully. Never make recovery depend on a chain of deltas.
- Retain the configured history after successful saves, but prompt at startup only for an unclean shutdown or a checkpoint newer than the primary file.
- Startup offers the newest valid recovery first and a cartridge-owned history browser showing timestamp, document, and saved/unsaved/conflicted status. Corrupt generations are skipped with a visible warning, and the next valid generation remains recoverable.
- Restoring recovery always produces a dirty document while preserving its original filename, format, and previous file fingerprint.
- On the first automatic primary-save conflict or failure, retain checkpoints, stop automatic overwriting for that document, and show one actionable cartridge alert rather than repeating failures every interval.

## Markdown

- Implement GitHub Flavored Markdown against the official [GFM specification](https://github.github.com/gfm/) using a pinned, vendored [cmark-gfm](https://github.com/github/cmark-gfm) parser so builds remain reproducible and offline.
- Keep Markdown source as the authoritative representation with a source-span AST and a rendered editor projection.
- Source mode edits exact UTF-8 Markdown. Rendered edits update semantic nodes and regenerate only the smallest affected source region; untouched source remains byte-for-byte stable.
- Support headings, emphasis, strong text, strikethrough, links, images, block quotes, lists, task lists, code, tables, autolinks, thematic breaks, and front matter.
- Preserve raw HTML, link destinations, image destinations, and front matter. Never execute HTML, scripts, styles, event handlers, links, or network/local image fetches.
- Render links and images as inert text/placeholders. Show front matter as an inert rendered block editable in source mode.
- Encode FairyWriter underline and alignment as valid inert raw-HTML presentation markup and render only the explicitly supported safe subset through FairyWriter’s own renderer.
- Accept `.md` and `.markdown` on load; Save As creates `.md`.

## Test Plan

- Add `fairywriter_persistence` for deterministic component and filesystem tests and `fairywriter_persistence_process_e2e` for the real lifecycle, raising the production gate from 7 to 9 tests.
- The process test launches the actual test-enabled production executable, injects ordinary cartridge input, creates and saves a real ODT, closes normally, launches a fresh process, opens it, and verifies content and formatting.
- Repeat the lifecycle for DOCX, RTF, and Markdown. Markdown tests verify both rendered and source editing.
- Kill a process after a durable checkpoint, relaunch, select recovery through the cartridge UI, verify the restored document is dirty, save it, and verify it from another fresh process.
- Exercise recovery rotation at 0, 1, 5, and 255; adjacent deduplication; A→B→A history; cursor-only activity; formatting-only edits; corrupted-newest fallback; clean-shutdown suppression; manual checkpoint with timed saving disabled; and retained post-save history.
- Cover first Save, Save As New, overwrite confirmation, extension enforcement, failed-write cleanup, read-only targets, missing directories, Unicode filenames, external replacement with unchanged timestamps, concurrent instances, disk-full behavior, and atomic-rename failure using real temporary filesystems rather than mocked writers.
- Run the complete production build and 9/9 gate natively on macOS/APFS and in the Linux production job. Add a macOS CI job alongside the existing Linux gate.
- Run the upstream GFM conformance corpus plus FairyWriter round trips. Require semantic equivalence after rendered edits and byte equality for untouched Markdown source.
- Keep packaged macOS and Linux visual acceptance explicit: automated process tests establish persistence behavior; final cartridge dialog layout and packaged-app interaction receive a short manual smoke test on each platform.

## Locked Defaults and Assumptions

- Native default and first-save default: ODT.
- Autosave mode: Save + Recovery.
- Interval: 1 minute, configurable through 255 minutes.
- Recovery copies: 5, configurable from 0 through 255.
- Recovery versions and ordinary Undo/Redo remain separate histories.
- Recovery history survives successful saves but does not itself cause a startup prompt.
- Full Markdown support is included in this hardening work, not deferred.
