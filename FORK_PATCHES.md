# Fork patches

Every change to this fork, newest first. Base: `4.7.2-stable` (`ed1daf0bf0`), branch `goblin-kitchen`.
For each patch: date, commit, files, why the game needs it, and whether upstream has an equivalent.
Prefer game code, then addons, then `modules/`, and touch `core/` last.

| date | commit | area | change | why | upstream |
|---|---|---|---|---|---|
| 2026-09-28 | see `git log -- modules/jolt_physics/spaces/jolt_job_system.*` | modules/jolt_physics | `JoltJobSystem` counts live jobs; new destructor reclaims jobs still finishing on worker threads before the job free list is destroyed. Files: `spaces/jolt_job_system.{h,cpp}` | Jobs release themselves after the step barrier returns, so the final step can leave jobs unreclaimed. Dev builds then hit Jolt's `FixedSizeFreeList` assertion at exit in about 1 of 6 headless runs, which made harness tests flaky. Verified: 2/12 runs failed before, 0/30 after. | Not fixed as of upstream master 2026-09-28. Candidate to upstream. |
| 2026-09-27 | (this file) | docs | Added FORK_PATCHES.md | Track fork changes for future cherry-picks | n/a |
