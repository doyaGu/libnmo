# Agent Instructions

## Git Workflow

- Do not use git worktrees in this repository unless the user explicitly re-authorizes them for the current task.
- Work in the current checkout by default.
- Stage exact files or hunks only. Do not broad-stage the repository.
- Do not stage or commit documentation files unless the user explicitly requests documentation changes.

## Commit Messages

- Use the repository's existing imperative subject style.
- Start the subject with a capitalized verb.
- Do not use Conventional Commit prefixes or scopes.
- Avoid subjects like `feat: ...`, `fix: ...`, `tests: ...`, `type: ...`, `docs: ...`, or `extension: ...`.
- Good examples:
  - `Add write semantic probe`
  - `Assert CLI write command semantics`
  - `Tighten default vtable provenance`
- Keep commits fine-grained when the user asks for incremental commits.
