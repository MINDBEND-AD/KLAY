# How to start the build with Claude Code

Unpack this folder where the code should live (e.g. `C:\dev\KLAY`), run
`git init` in it, open Claude Code there (native Windows, not WSL — the build
needs MSVC), and go through the three steps below.

## Step 1 — Plan (one session, plan mode)

Switch to plan mode (Shift+Tab until it shows plan mode), then paste:

```
Read CLAUDE.md, then docs/spec.md sections 6, 7, 9, 10, 12 and 13, and look at
the images in docs/wireframes/.

Write docs/plan.md: the implementation plan for the MVP (milestones M0–M5 in
spec section 13). For each milestone list:
- the tasks in build order, each small enough for one focused session step
- the requirement IDs each task covers
- how each task is verified (test, pluginval, render-CLI null test, or a manual
  check by me)
- the milestone's exit criterion, phrased so it can be checked

Also list, before M0, anything in the spec you find ambiguous, contradictory or
missing for the MVP, as questions for me. Don't write any code yet.
```

Review `docs/plan.md`, answer the questions, and let Claude correct the plan.
Commit it.

## Step 2 — Build, one milestone per session

Start a fresh session for each milestone and switch to auto mode, so routine
steps don't need your approval. Then set the milestone as a goal. Setting a
goal starts the work right away, and Claude keeps going until the condition is
met. The goal is checked against what appears in the conversation, so the
condition says how Claude proves it:

```
/goal Milestone M0 in docs/plan.md is complete. Work task by task, follow
CLAUDE.md and commit after each task. Done means: every M0 task is ticked in
docs/plan.md, and the final build, test and pluginval (strictness 8) runs are
shown passing in this conversation. If a task needs a manual check by me
(listening test, DAW smoke test), write the steps into docs/checks/M0.md and
stop there. Stop after 60 turns at the latest.
```

Repeat with M1 … M5, changing the milestone in the goal.

## Step 3 — Review at each milestone end

Before you sign a milestone off:

```
Review all changes since the last milestone tag against docs/spec.md and
CLAUDE.md, with a fresh-eyes subagent. Report real-time safety violations,
requirements marked done without verification, and deviations from the spec.
```

Then run the manual checks yourself, and tag the commit (`git tag m0-done`).

## Your checkpoints

| Milestone | What you check yourself |
| --- | --- |
| M0 | The empty plugin loads in 3 hosts |
| M1 | A hand-built kick plays in key; listening test LT-1; assumption checks AC-1…AC-5 and LT-2 |
| M2 | Roll a kick and refine a layer (UC-1, UC-2) |
| M3 | Kick page UI against the wireframe |
| M4 | FX chains by ear and against the FX wireframes (UC-4) |
| M5 | Presets and Drag Master export into your DAW (UC-6, UC-7) → MVP |

## When the spec changes

Edit the spec in Claude Docs, export it again as Markdown, replace
`docs/spec.md`, and tell Claude Code which sections changed.
