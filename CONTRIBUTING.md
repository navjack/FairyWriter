# Contributing

FairyWriter welcomes bug reports, design discussion, tests, and code changes.
The project is early, so preserving its few strong invariants matters more than
maintaining accidental implementation details.

## Before changing code

Read these in order:

1. `SCRATCH.MD`
2. `plan.md`
3. `DEVELOPMENT_STATUS.md`
4. `docs/HANDOFF_2026-07-26.md`
5. `docs/SNES_FRONTEND.md`

The visible product is a source-generated SNES cartridge on a fixed 256x224
screen. The host owns documents and platform services; it must not replace
cartridge UI with native lookalikes.

## Development flow

1. Prepare the dependency with `./scripts/bootstrap-snesrecomp.sh`.
2. Make the smallest structurally correct change.
3. Add a regression that exercises real behavior.
4. Run the full production test gate.
5. Update `SCRATCH.MD`, `plan.md`, or `DEVELOPMENT_STATUS.md` when the change
   affects current state, evidence, or next work.

Do not commit ROM files, packaged applications, personal documents, private
artwork, credentials, or generated build directories.

## Pull requests

Keep each pull request focused and explain:

- the invariant being fixed or extended;
- the root cause or ownership decision;
- the tests run and their results;
- any remaining physical-platform or live-GUI acceptance.

Draft pull requests are welcome. A green automated build is not evidence of
real Windows, Steam Deck, keyboard, mouse, or recovery behavior unless that
behavior was actually exercised.

By contributing, you agree that your contribution is licensed under
GPL-3.0-or-later.
