# Versioning FairyWriter

FairyWriter uses [Semantic Versioning](https://semver.org/) for public
releases. `VERSION` is the one authoritative product version. CMake, executable
metadata, package builders, package filenames, and CI artifact names all read
that file.

Public release tags use the same number with a `v` prefix:

```text
VERSION: 0.1.0
tag:     v0.1.0
```

The version remains at the latest public release during ordinary development.
Development builds are identified by their Git commit and GitHub Actions run,
not by changing the product version as a side effect of repository state.

## Before 1.0

- Increment **PATCH** for backward-compatible fixes and packaging corrections.
- Increment **MINOR** for new user-visible behavior, format support, or
  substantial cartridge/runtime changes.
- Reserve **1.0.0** for the first release whose supported platforms and core
  document workflows have completed their stated manual acceptance.

GitHub's prerelease flag communicates tester-preview status. It does not change
the SemVer number stored in `VERSION`.

## Release checklist

1. Choose the version from the actual change scope.
2. Update `VERSION`.
3. Move the relevant `CHANGELOG.md` entries from **Unreleased** into a dated
   heading for that exact version.
4. Add the version/date entry to the AppStream release history.
5. Merge only after the three native production package jobs pass.
6. Use the retained DMG, AppImage, ZIP, and checksum sidecars from one green
   post-merge `main` run.
7. Create `vMAJOR.MINOR.PATCH` on that exact commit and publish the matching
   GitHub Release.
8. Verify the rendered release page, target commit, prerelease state, asset
   names, and GitHub-reported SHA-256 digests.

Never move an existing release tag or replace published assets in place. If a
release is wrong, correct the source and publish a new patch version.
