# Third-party notices

FairyWriter is derived from FocusWriter 1.9.0 by Graeme Gott and contributors.
The inherited source is GPL-3.0-or-later; original copyright and SPDX notices
are retained in the source tree.

The build and application use:

- Qt, under its applicable commercial, LGPL, or GPL terms;
- Zlib;
- the `qtzip` code retained in `src/3rdparty/`;
- an external pinned `navjack/snesrecomp` checkout. FairyWriter carries only
  its own compatibility patch; see that upstream repository for its source
  notices and third-party attribution;
- miniaudio 0.11.25 by David Reid, vendored as the single header
  `third_party/miniaudio/miniaudio.h` and used only for audio output. It is
  offered under a choice of public domain (Unlicense) or MIT-0; both texts are
  at the end of that file.

Removed in the retirement of the FocusWriter-derived desktop UI, and listed here
only so their absence is not mistaken for an oversight: Oxygen icon artwork
(LGPL-3.0-or-later) and the Blasphemous typeface by Patrick H. Lauke (CC BY 3.0).
Neither asset nor its license file remains in the tree, and neither was ever part
of the shipped cartridge product — the macOS bundle prune removed them before
packaging. `CREDITS.md` retains the full inherited FocusWriter credits,
translators included, because FairyWriter is still a derivative of that work.

This file is a practical index, not a replacement for the license texts and
source-level notices.
