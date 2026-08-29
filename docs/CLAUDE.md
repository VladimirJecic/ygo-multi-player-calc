# docs/

The guides. Keep them true — they are the only thing standing between the next agent and
re-walking 230 versions of dead ends.

```
ARCHITECTURE.md         how the mod gets in, what it hooks, how it is put together
DESIGN.md               the layout rulebook: every screen, every alignment rule, and why
BUILD.md                toolchain, the build/sign/install loop, test automation
IL2CPP.md               metadata format, hooking technique, the classes that matter
HISTORY.md              timeline, dead ends, bugs and their real causes, current state
STRATEGY-unique-names.md  the previous agent's plan for US-04 (a proposal, not a finding)
```

## Rules

1. **A finding goes in a doc, not in a log.** The moment you learn something durable about the
   game — an offset, a stripped method, why a skin behaves differently — write it here and
   delete the log you found it in.
2. **Write down the failure, not just the fix.** "Fit to the plate, not the rect" is useless
   without "because ARC-V draws a shield taller than its rect and the panels overlapped".
   Every rule in `DESIGN.md` names the regression it prevents; keep it that way.
3. **When a dead end is confirmed, add it to `HISTORY.md`.** An approach that was tried and
   reverted is more valuable to the next agent than one that worked.
4. **Update `HISTORY.md`'s "Current state" after every user test.** That section is what a
   fresh session reads to know what is broken right now.
5. Keep them in English, like the code comments. `DSH.md` and `userstories/` stay in Serbian —
   those are the user's own words.
