# CLAUDE.md

Guidance for Claude Code and other AI assistants working in this repository.

## Status: empty repository

**As of 2026-09-12 this repository contains no code.** The remote
(`https://github.com/fortebio/ToolEngineer`) has zero branches and zero commits;
this file is the first content committed to it.

Everything below the "Repository facts" section is a **placeholder**. Do not
treat it as a description of an existing system, and do not answer questions
about "how this codebase works" from it. The first substantive change to this
repo should replace those placeholders with the real structure — see
"Updating this file" at the bottom.

## Repository facts

| | |
|---|---|
| Remote | `https://github.com/fortebio/ToolEngineer` |
| Owner | `fortebio` |
| Default branch | none yet (created by the first push) |
| Language / stack | not yet chosen |
| Build system | none |
| Tests | none |
| CI | none configured (`.github/workflows/` does not exist) |

## Branch and commit workflow

These conventions apply to automated sessions working in this repo:

- Develop on a dedicated feature branch; never commit directly to the default
  branch once one exists.
- Branches created by Claude Code sessions use the `claude/<topic>-<suffix>`
  form (this file was committed on `claude/claude-md-docs-namqct`).
- Push with `git push -u origin <branch-name>`.
- Do not open a pull request unless explicitly asked.
- Commit messages: short imperative subject line, body explaining *why* when the
  change is not self-evident.

## Placeholders — fill these in with the first real code

Each heading below is a section a useful CLAUDE.md needs. Replace the italic
prompt with the actual answer; delete the section if it genuinely does not apply.

### Project purpose

*One paragraph: what ToolEngineer does and who uses it.*

### Repository layout

*Top-level directories and what belongs in each. Point at the entry point(s).*

### Setup

*Exact commands to get from a fresh clone to a working environment — runtime
version, dependency install, any required environment variables or secrets and
where they come from.*

### Build, test, lint

*The exact commands a contributor runs locally, and which of them CI enforces.
Include how to run a single test, not just the whole suite — that is the command
an assistant needs most often.*

### Architecture

*The handful of things that are not obvious from reading one file: the main
abstractions, how requests/data flow through them, and which boundaries matter.*

### Conventions

*Anything a new contributor would otherwise get wrong: naming, error handling,
logging, how configuration is threaded through, what must never be edited by
hand (generated files, lockfiles), and how to regenerate those.*

### Gotchas

*Known sharp edges — slow steps, flaky areas, things that look wrong but are
deliberate.*

## Updating this file

Keep this file accurate rather than exhaustive; a stale instruction is worse than
a missing one. When you land a change that alters the layout, the commands, or a
convention above, update the corresponding section in the same commit. Remove the
"Status: empty repository" section as soon as real code exists.
