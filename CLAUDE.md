# KLAY â€” project rules for Claude Code

KLAY is a layer-based kick drum VST3 instrument (sample player in v1). The full
requirements live in `docs/spec.md` (v0.6); wireframes are in `docs/wireframes/`.
The build order and task breakdown live in `docs/plan.md` once it exists.

## How to work
- The spec is the source of truth. Before implementing anything, read the spec
  section it belongs to (use the section map below; don't load the whole file
  unless you need it). If the spec is silent or contradicts itself, stop and ask
  instead of guessing.
- Work one milestone at a time (spec section 13, M0â€“M5 = MVP). Don't start work
  from a later milestone.
- Cite requirement IDs (FR-, NFR-, UI-, C-) in commit messages and in code comments
  where a requirement drives a non-obvious decision.
- A task is done only when it builds, its tests pass, and the requirement's
  verification from spec section 12 is met. Don't mark tasks done on "it compiles".
- Commit after each completed task with a short message; never rewrite history.
- Git identity: never set or change git `user.name` / `user.email`, and never write
  personal email addresses into files. Commits use the GitHub noreply address from
  the repo's local git config.
- Before every push, check that `git log --format='%ae %ce'` for the commits being
  pushed shows only `@users.noreply.github.com` addresses; if any other address
  appears, stop and ask.
- Update `docs/plan.md` (tick tasks, note deviations) as you go.

## Fixed technical decisions (don't change without asking)
- C++20, JUCE 8 under AGPLv3, CMake, single VST3 target, Windows 10+ x64, MSVC.
  Keep the core platform-neutral (C-007): JUCE API only in `plugin/`, `ui/`
  and adapters; `core/` stays JUCE-free where practical.
- Free, open-source dependencies only. No paid or proprietary libraries.
- Every GPL-family file or dependency carries an SPDX header, the marker comment
  `LICENSE-GPL`, and an entry in `THIRD_PARTY_LICENSES.md` (C-006).
- Time-stretch / length-preserving pitch: Signalsmith Stretch (MIT).
  Repitch: JUCE LagrangeInterpolator. Key detection: own YIN + FFT cross-check.
- Oversampling: `juce::dsp::Oversampling`, only in harmonic-generating stages;
  EQ and filters use matched biquads after Vicanek (FR-212â€“215).

## Real-time rules (NFR-001, FR-234)
- The audio thread never allocates, locks, does file I/O or logs.
- UI â†” audio communication goes through lock-free FIFOs or atomics (NFR-013).
- All parameters are smoothed (FR-226); parameter IDs never change once
  released (FR-235).

## Verification
- Unit tests: Catch2 or GoogleTest. Plugin validation: pluginval (strictness 8+)
  and the Steinberg VST3 validator. Golden-file tests via `tools/render-cli`.
- Run the build and the tests before saying a task is finished.
- Listening tests (LT-1, LT-2, AC-5) and DAW smoke tests are done by the
  product owner: prepare them, then stop and ask.

## Spec section map (docs/spec.md)
- 2 vision Â· 4 use cases (UC-1â€¦UC-7) Â· 5 glossary
- 6 platform & integration (C-001â€¦C-007, FR-001â€¦FR-007)
- 7.1 layer engine Â· 7.2 pitch & key Â· 7.3 randomization Â· 7.4 sample library
- 7.5 FX rack Â· 7.5.1 common module rules Â· 7.5.2 Distortion Â· 7.5.3 Equalizer
  Â· 7.5.4 Filters Â· 7.5.5 Finalizer (FR-300â€¦FR-334)
- 7.6 presets Â· 7.7 export Â· 7.8 volume envelope editor
- 8 UI/UX (UI-001â€¦UI-019, wireframes) Â· 9 non-functional (NFR-001â€¦NFR-016)
- 10 architecture & module layout Â· 11 licensing Â· 12 testing & acceptance
- 13 MVP scope & milestones Â· 14 decision log Â· 15 user manual notes
