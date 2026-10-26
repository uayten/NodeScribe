---
title: "The README opens with one running paragraph, not a feature list"
author: Antônio
date: 2026-10-26
branch: main
cost: { minutes: 18, tokens: 99721 }
id: l-936b052e
scope: repo
status: active
src: agent
touches:
  - README.pt-BR.md
  - README.md
---

## Decided
- The README's opening is one running paragraph that says why the plugin exists, the problem it solves (the ~1,000-token clipboard node and its 32-character link identifiers) and how it is used, ending with one sentence on what else it reads and writes. The user asked for this in place of the bulleted feature list, and approved the result. A new capability goes into that last sentence, not back into a list.
- Each argument appears once: the "saving comes from eliminating discovery, the text is ~2% of the cost" reasoning lives only in *Principles*. Repeating it across sections is what made the old README long.

## Watch out
- `README.pt-BR.md` is the original; `README.md` is translated from it in the same commit. Developer material does not go back into either: it belongs in `Docs/development.md`, English only.
