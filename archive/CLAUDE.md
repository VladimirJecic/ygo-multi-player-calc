# archive/ — superseded source, kept for reference

**Never build from here. Never copy from here without reading `docs/HISTORY.md` first.**

```
mod.c.3player   the 3-player variant, 2026-08-26. Relevant to the open report that
                3-player mode is not centred and should go back to the v224 arrangement —
                but it is a snapshot of a branch, not a fix.
mod.c.v79       mod.c at v79 (2101 lines vs 5173 now). Useful only to see how a subsystem
                looked before it grew; most of what it does was since found to be wrong.
```

Anything in here is a photograph, not a fallback. If something in the current `mod.c` is
broken, fix the current `mod.c` — do not revive a snapshot. If you need to know what changed,
`diff` against `src/native/mod.c` and write the finding into `docs/HISTORY.md`.

Nothing new goes in here unless it is a real branch point worth remembering. This is not a
place to park files you are unsure about — delete those.
