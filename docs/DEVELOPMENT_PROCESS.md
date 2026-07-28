# How FairyWriter was developed

FairyWriter was conceived and directed by Jack Mangano, and it was built with
extensive assistance from agentic large-language-model coding systems. This
document records both sides of that history: where the idea and decisions came
from, and how AI agents helped turn them into a tested public codebase.

## The idea came first

The product ideas behind FairyWriter came from Jack. The initial inspiration
came from a friend who wanted a word processor that looked like it belonged on
the Super Nintendo.

There are many easy ways to imitate that appearance with a desktop theme,
pixel-art controls, or a framebuffer effect. Jack arrived at a more literal and
more interesting answer:

> If the goal is a word processor that looks like it runs on the SNES, make a
> word processor that actually runs on the SNES.

That decision became the project's defining invariant. FairyWriter would not be
a normal Qt editor hidden behind a cosmetic console skin. Its visible 256x224
interface, menus, help, caret, selection, formatting controls, and input
behavior would be owned by a real source-generated SNES cartridge. A native host
would provide the services that a cartridge should not own: complete documents,
file formats, atomic saves, recovery, the clipboard, and operating-system
integration.

Jack supplied the creative direction, evaluated the experience, made product
choices, and decided what counted as FairyWriter. Agentic coding systems helped
work out how to implement and verify those decisions.

## Agentic LLM systems used

Development used multiple model families and agent styles rather than relying
on one assistant:

- Claude Fable, Opus, and Sonnet
- Gemini Flash and Pro
- ChatGPT 5.6 Luna, Terra, and Sol

These systems were used as coding agents: they could inspect the repository,
follow plans and handoffs, edit multiple files, run builds and tests, interpret
failures, and continue work across bounded tasks. Different models contributed
at different times and in different roles. This project does not claim that
every model worked on every subsystem or that every generated suggestion was
accepted.

The model names above are recorded as the project creator supplied them. Their
inclusion is a development disclosure, not an endorsement by or affiliation
with the model providers.

## What the agents helped do

Agentic assistance was used throughout the engineering process, including:

- studying the inherited FocusWriter 1.9.0 document and persistence code;
- designing and implementing the cartridge/host ownership boundary;
- generating the 32 KiB LoROM image from source;
- implementing 65816 editor, rendering, menu, help, caret, selection, proofing,
  formatting, scrolling, and input paths;
- integrating the XBAND keyboard and Super NES Mouse protocols into the
  `snesrecomp` runtime;
- designing the bounded SRAM command, event, and double-buffered viewport
  protocol;
- adapting the native document engine, file catalog, recovery, and atomic save
  behavior;
- writing targeted regressions for machine-code, framebuffer, mailbox,
  long-document, ODT, recovery, and file-boundary behavior;
- diagnosing timing-sensitive and register-width defects in the real 65816
  execution path;
- building and auditing macOS and Linux packages and preparing Windows support;
- maintaining plans, handoffs, status records, architecture documentation, and
  tester instructions;
- auditing the repository for private material, secrets, built artifacts,
  licensing context, and fresh-clone reproducibility before public development.

The assistance was therefore substantial. It was not limited to autocomplete
or isolated snippets; agents participated in long-running implementation and
verification loops across the codebase.

## How human direction remained authoritative

The agents did not originate the FairyWriter concept or independently decide
the product's meaning. Human direction supplied the goals and constraints:

- the visible interface must be cartridge-owned;
- the virtual screen remains exactly 256x224;
- no commercial ROM data is shipped;
- document and filesystem state has one explicit native owner;
- timing-sensitive fixes must preserve typing throughput;
- platform parity must be real rather than cosmetic;
- automated evidence must not be presented as physical-device or live-GUI
  proof;
- private writing, local captures, and build artifacts stay out of public
  history unless explicitly authorized.

Jack also made the subjective decisions that tests cannot make: whether the
interface felt right, whether the result matched the original idea, which
tradeoffs belonged in the product, and when a piece of work was ready to keep.

LLM output was treated as proposed engineering work, not as an authority. Code
could be revised, rejected, or replaced when it violated an invariant, performed
poorly, or failed to establish real behavior.

## Plans and durable handoffs

The work was too broad to depend on one chat session or one model's context
window. FairyWriter therefore developed a written continuity system:

- `SCRATCH.MD` is the live external working memory;
- `plan.md` records goals, ordered work, gates, and deferred scope;
- `DEVELOPMENT_STATUS.md` records implemented behavior and open evidence;
- `docs/HANDOFF_2026-07-26.md` preserves a detailed historical handoff;
- `docs/SNES_FRONTEND.md` records the guest/host ownership contract.

Agents were instructed to read those files before making changes and to update
the current state after meaningful work. Superseded discoveries were archived
instead of silently discarded. This allowed Claude, Gemini, and ChatGPT agents
to continue one another's work without repeatedly resetting the architecture or
losing hard-won low-level constraints.

That external memory is part of the engineering method, not disposable process
debris. It also makes the development history unusually inspectable for future
human contributors.

## Tests, not confidence, decided correctness

Model confidence was never accepted as proof that low-level behavior worked.
The project repeatedly required agents to reproduce a defect with a targeted
regression before repairing it and to validate the real execution path
afterward.

Examples include:

- running the generated cartridge through an actual 65816 interpreter;
- checking exact machine-code sequences and ROM layout;
- exercising XBAND scan-code transactions and Super NES Mouse reports;
- validating mailbox bounds, revisions, ordering, and integrity;
- rendering real framebuffers and measuring pixels or tile attributes;
- exercising a substantial standards-shaped ODT through import, editing,
  undo/redo, save, and reopen;
- repeating timing-sensitive keyboard tests after a seemingly harmless render
  loop grew too slow;
- launching Linux packages under both X11 and Wayland;
- distinguishing automated package evidence from human keyboard, mouse,
  recovery, Windows, and physical-device acceptance.

This mattered because plausible-looking LLM code can still be wrong in exactly
the ways this project makes visible: one-byte branch displacement overflow,
8-bit/16-bit register-width confusion, stale tile-plane cells, partially
published mailbox state, or an abstraction that moves ownership to the wrong
side of the cartridge boundary.

## Cross-model work as review

Using several model families also created a form of practical cross-review.
Later agents inherited source, tests, status files, and unresolved failures
rather than only the prose explanation from an earlier assistant. They could
challenge a prior assumption by running the code, reading the machine state, or
adding a more discriminating regression.

This was useful, but it was not automatically independent or unbiased review.
Models can repeat the same attractive mistake, especially when they inherit the
same framing. The repository's explicit invariants and executable tests were
the stronger source of independence.

## From private experiments to public development

The public repository was prepared with the same human-directed agentic
workflow. The process identified old packaged applications in private Git
history, a personal manuscript used as an ODT fixture, a runtime dependency that
only existed as a neighboring dirty checkout, and public documentation that
still described the inherited FocusWriter product.

The resulting public baseline:

- started from a clean root commit while preserving the private local history;
- replaced the manuscript with a deterministic public ODT generator;
- made the pinned runtime and FairyWriter peripheral patch reproducible;
- added current project, architecture, build, contribution, security, credit,
  and third-party documentation;
- passed fresh-clone macOS and GitHub/Linux production gates;
- passed a complete public-history secret and artifact audit.

The H.264 MP4 showcase embedded in the README was subsequently included with
Jack's explicit authorization. It contains no audio or private
desktop/document content. The same MP4 is retained in `docs/media/` as a
repository-owned fallback for the GitHub-hosted inline player.

## Attribution and responsibility

The appropriate summary is:

- Jack Mangano conceived FairyWriter, directed it, supplied its ideas and
  product judgments, and accepted responsibility for what entered the project.
- A friend inspired the original desire for an SNES-looking word processor.
- Claude Fable, Opus, and Sonnet; Gemini Flash and Pro; and ChatGPT 5.6 Luna,
  Terra, and Sol supplied extensive agentic coding assistance.
- FocusWriter by Graeme Gott and its contributors supplied the inherited
  document and desktop foundation.
- The checked-in source, tests, licenses, credits, and Git history remain the
  authoritative record of the result.

LLM systems are tools, not copyright holders or maintainers. Bugs and design
decisions remain the responsibility of the human project and its contributors,
regardless of whether a particular implementation originated in a human-written
patch, an agent proposal, or collaboration between the two.
