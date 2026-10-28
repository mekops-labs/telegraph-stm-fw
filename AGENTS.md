# Telegraph firmware — AI Agent Instructions

Read `README.md` first. It is the source for build, flash, and layout.

## Confidentiality

This is a public repository. Do not write internal planning data into code,
comments, commit messages, docs, or configuration:

- No milestone labels, phase numbers, or sprint items.
- No internal spec or section references.
- No names or paths of private repositories or documents.
- No decision-record IDs.

State the technical fact directly. Public-standard references (for example
`RFC 8949 §4.2`) are permitted.

## Private data

Do not write real IP addresses, MAC addresses, Wi-Fi SSIDs, or credentials into
any file, including logs, shell examples, and config snippets. Write
`<redacted>` in place of the value and keep the technical fact.

## Build

- Build the STM32 firmware with `make build CONFIG=ipc`, and the engine
  firmware of the edge MCU with `make edge-build`. Both run in containers.
- Do not run `idf.py`, `podman`, a cross-compiler, or the NuttX `make`
  directly.
- Run the host tests with `make test`, and the formatter, `tidy` and `cppcheck`
  targets before a commit.
- If the engine build does not support a necessary operation, stop and tell the
  user.

## Code

- C sources obey ISO C99 or later. Do not use GNU extensions, `__attribute__`,
  or `__builtin_*` outside the board port.
- Format C with `clang-format` (LLVM base style, 4-space indent).
- Keep a comment to one line, three lines at most.
- Do not write edit-history comments (for example "no longer", "was X, now Y",
  "legacy"). Write only what the code does now.

## Commits

- Subject format: `{type}[({scope})]: {title}`, as in the history. Types:
  `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci`, `chore`.
- One logical change per commit.
- Body: one `- bullet` for each line, 80 characters at most.
- No `Co-Authored-By` trailers.
