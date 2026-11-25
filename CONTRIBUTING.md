# Contributing to aos-linphone

[Project overview](README.md) | [Architecture](ARCHITECTURE.md)

Two git hooks in `.githooks/` keep the code style and the commit messages
consistent. Enable them once in each clone:

```sh
git config core.hooksPath .githooks
```

## Code style

The code style lives in `.clang-format`: opening braces stay on the line of
the function or statement, indentation is four spaces, and line breaks are
left to the author.

On every commit the pre-commit hook fixes the staged text files and stages
the result. It turns CRLF line ends into LF, removes trailing spaces and
tabs, adds a missing final newline, and replaces typographic quotes, dashes,
arrows, ellipses and no-break spaces with ASCII. C and C++ sources then go
through `clang-format` with that style; when `clang-format` is not
installed, the hook skips that step. Patch files (`*.patch`, `*.diff`) stay
as they are, because their context lines must match the upstream sources.

The hook works on the staged version of each file, so a partly staged file
commits only its staged part. When nothing is left to commit after these
fixes, the commit stops with an error.

## Commit messages

The commit-msg hook accepts messages in the
[Conventional Commits](https://www.conventionalcommits.org/) form
`type(scope): subject`, for example
`feat(compat): add poll() over WaitSelect`. The type is one of `feat`,
`fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`
and `revert`; the scope is optional and lower-case, and `!` after the type
or scope marks a breaking change. The subject line takes at most 100 bytes,
which is 100 characters of plain ASCII. Messages that git writes itself
for merges, reverts and `fixup!`, `squash!` or `amend!` commits pass as they
are.
