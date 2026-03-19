---
name: phoenix-rpi-knowledge-base
description: Use when updating the documentation set for the Phoenix RTOS Raspberry Pi port, preserving findings across long sessions, indexing upstream source material, adding references, or enriching the project knowledge base for future agents.
---

# Phoenix RPi Knowledge Base

Use this skill when the main output is knowledge preservation rather than code.

## Read First

1. `AGENTS.md`
2. `docs/README.md`
3. `docs/status.md`
4. `docs/source-artifacts.md`

## Main Goal

Make future sessions cheaper and less error-prone.

## Workflow

1. Add information to the most specific document possible.
2. Keep `docs/status.md` current as the short handoff.
3. Keep `docs/source-artifacts.md` current as the source index.
4. When new information is time-sensitive, mark it `Re-verify:`.
5. Prefer exact upstream paths and URLs over summary-only prose.
6. If a document grows too large, split by concern rather than by date.

## What Belongs Where

- current state and next steps:
  `docs/status.md`
- architecture and staged plan:
  `docs/implementation-dossier.md`
- manual prerequisites, operator setup, and human-only steps:
  `docs/manual-operator-instructions.md`
- board-specific notes:
  `docs/platforms/*.md`
- testing and lab design:
  `docs/testing-automation.md`
- important upstream sources:
  `docs/source-artifacts.md`

## Rules

- Do not let important knowledge live only in chat history.
- Do not create redundant markdown files with overlapping purpose.
- Do not rewrite source facts from memory when you can point to the exact upstream path.
