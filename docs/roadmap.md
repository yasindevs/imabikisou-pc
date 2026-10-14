# Roadmap and progress methodology

Snapshot: 14 October 2026. No release date is promised.

## How the percentages work

The figures describe estimated engineering readiness within each workstream. They are manually assessed milestones, not measurements of game-wide correctness. The prototype has only been exercised through the opening and first classroom.

| Workstream | Estimate | Weight in overall score |
|---|---:|---:|
| Boot & runtime | 90% | 10% |
| Graphics & pacing | 65% | 20% |
| Input & save/load | 65% | 10% |
| Audio | 35% | 15% |
| Movies | 35% | 15% |
| Full-game validation | 10% | 20% |
| English story coverage | 33 / 19,018 = 0.17% draft | 10% |

Overall = sum(estimate × weight) / sum(weights), rounded: **41%**. Translation coverage counts draft entries, not reviewed quality. The chart's remaining portion means work not yet considered ready, including untested behavior. It is not a prediction of hours remaining.

Edit `docs/progress.json`, then run `python tools/render_progress.py` to regenerate the chart (Pillow required). The README table and prose must be updated with the same evidence. Do not increase a percentage solely because FPS increased.

## 1. Stable opening

- [x] Pass startup barriers and early callbacks.
- [x] Display fonts and first story scenes.
- [x] Repair confirmation input and initial save naming.
- [x] Separate display clock from GPU work and remove duplicate presentations.
- [ ] Remove upstream PCM interruptions from the opening jingle.
- [ ] Meet movie decode deadlines without pausing sound.
- [ ] Repeat the same test sequence without diagnostic overhead and retain comparison logs.

Exit criterion: a reproducible opening-to-classroom run with responsive controls, correct text, stable presentation and no audible crackling. This milestone is **not complete**.

## 2. Playthrough validation

- [ ] Inventory scenes, routes and choices; track tested versus untested paths.
- [ ] Check backgrounds, overlays, transitions and special effects.
- [ ] Validate multiple save slots, reloads and longer sessions.
- [ ] Diagnose remaining freezes and crashes with reproducible steps.

Exit criterion: documented full-route testing rather than extrapolation from the prologue.

## 3. English machine-translation preview

- [x] Export stable script IDs and rebuild the archive with structural verification.
- [x] Load a separate English preview with isolated save/settings files.
- [x] Render 33 draft entries and 27 menu/dialog labels in the local prototype.
- [ ] Inventory text baked into UI images.
- [ ] Establish a glossary, context grouping and machine-translation workflow.
- [ ] Translate the main script in batches while preserving control tags.
- [ ] Review text layout, choices, missing glyphs and terminology in-game.

Exit criterion: explicitly labeled machine-translated coverage with technical validation. Human linguistic review is separate from entry counts.

## 4. Reproducible developer build and release preparation

- [x] Prepare public integration sources and a runtime patch with a pinned upstream base.
- [x] Check that the patch applies to that base.
- [ ] Rebuild the native port from a clean clone and privately regenerated inputs.
- [ ] Automate the local input-generation workflow and document exact PPU/SPU steps.
- [ ] Define a release package that excludes all game content.
- [ ] Record a progress video and publish release notes describing unresolved issues.

The current build worked in the development checkout. That does not yet establish a clean-clone native build.
