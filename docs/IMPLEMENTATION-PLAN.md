# Dionite — Implementation Plan & Tracker

This file is the single source of truth for the production build. Every work
session starts by reading the **Status board** and **Next action**, and ends by
updating the **Progress log**. If work is not on this page, it is not part of
the plan.

---

## North star

A complete, production-grade Dionite: open world, 12 classes, pets, fishing,
hunting, gathering, building, vehicles, spaceship, infinite planets, duels,
bazaar, and the full iOS presentation — with **no TODO stubs** and **no shipped
binary art or audio**. Everything visual and audible is generated procedurally.

## Non-negotiables

1. **iOS only.** One platform folder (`platforms/ios`), one entry point
   (`AppDelegate.swift`), one renderer (Metal), one build system (`project.yml`).
   No Windows, Android, desktop, Vulkan, GL, SDL, GLFW or CMake targets.
2. **The C++ core is portable and platform-free.** No Apple APIs, no `#ifdef`
   platform branches, no file paths that assume a host OS.
3. **One ABI.** `platforms/ios/Dionite/DioniteAPI.h` is the only boundary.
   POD-only, `extern "C"`, no exceptions across the boundary, no allocation in
   the per-frame path, one canonical accessor per data type.
4. **No stubs.** A declared function either participates in gameplay or is
   deleted. `grep` for `TODO`, `stub`, `not implemented`, `Will be expanded`.
5. **Content is data.** All balance/content lives in `assets/data/*.json`,
   validated at load. C++ never hardcodes content tables.
6. **No unrequested platforms, dependencies, build systems or asset pipelines.**
7. **Verification gates block progression.** A workstream is done only when its
   gate passes in CI. Never delete or weaken a check to make a gate pass.

## Locked content volumes

| Content | Count |
|---|---|
| Biomes | 9 |
| Classes × skills | 12 × 25 = 300 |
| Fish (+ boss fish) | 320 (+40) |
| Animals (tameable) | 60 (24) |
| Pets | 44 |
| Build pieces × materials | 120 × 5 |
| Vehicles | 18 |
| Planet archetypes | 40 |
| Weapon bases / charms / special perks | 90 / 40 / 60 |

## Status board

| WS | Scope | State | Gate |
|---|---|---|---|
| WS0 | iOS-only purge, ABI rewrite, Swift bridge rewrite, tracker, CI | In progress | C++ `-Werror` clean, verifier green, iOS workflow green |
| WS1 | Fixed-step determinism, ContentDb, save v4 (6 slots), platform seam | Not started | determinism + migration + fuzz tests |
| WS2 | Open world: streaming terrain, 9 biomes, water, weather, POIs | Not started | 9 biomes traversable in budget |
| WS3 | Combat breadth: 10 weapon families, damage types, charms, duels, bosses | Not started | every family damages; every boss telegraphs |
| WS4 | 12 classes, ranks, skill web, paragon, respec | Not started | bot clears campaign on all 12 |
| WS5 | Fishing, pets, hunting, tools/gathering | Not started | 320 fish catchable; bonuses wired |
| WS6 | Building, vehicles, spaceship, infinite planets | Not started | base→ship→1000 planets, no gen collisions |
| WS7 | Bazaar, factions, currencies, fate cards, pantheon, compendium | Not started | every currency has sources + sinks |
| WS8 | Full UIKit UI on the new ABI (all screens) | Not started | every action reachable by touch |
| WS9 | Terrain/post-FX shaders, parametric asset factory, audio expansion | Not started | offscreen render regression in CI |
| WS10 | Server migrations, records, market, duels, admin, hardening | Not started | API suite against Postgres |
| WS11 | CI split, packaging, benchmarks, docs | Not started | unsigned IPA + all suites green |

## WS0 — iOS-only purge and ABI rewrite (checklist)

### Remove
- [x] `CMakeLists.txt` (desktop build system and `DioniteDesktop`/`DioniteVerify`/`DioniteIOS` targets)
- [x] `src/platforms/desktop/main.cpp` (desktop entry point)
- [x] `src/platforms/ios/main.cpp` (dead shim kept only for CMake)
- [x] `src/Rendering/Renderer.{h,cpp}` (multi-backend interface + `NullRenderer`)
- [x] Cross-platform wording in `PostFXStack`, `Matrix.h`, `Logger.cpp`, `project.yml`

### Move
- [x] `src/platforms/desktop/verify_campaign.cpp` → `tests/core_verify.cpp`
  (CI test tool, **not** a platform target; it is the behavioural/leak gate)

### Replace with iOS-only equivalents
- [x] `src/Rendering/MeshData.h` — the `Vertex`/`Mesh` geometry containers that
  the terrain stack and boss arenas actually use, with no renderer abstraction
- [x] `scripts/test_core.sh` — host compile + verifier gate
- [x] `scripts/package_ipa.sh` — unsigned IPA used by CI

### ABI (rewrite)
- [x] ABI version 2, separate save-schema version
- [x] `DIBootParams` + status return + `dionite_last_error`
- [x] One `dionite_frame()` call per frame with pointers (camera, HUD, instances,
  damage numbers, view-projection) and no per-frame allocation
- [x] Stable `const char*` HUD/ability text + `dionite_text_revision()`
- [x] Batched event pop and batched audio drain
- [x] Edge-triggered dash/ability, held fire
- [x] `noexcept`, catch-all guards, `static_assert` on POD sizes/offsets
- [x] `dionite_save_json(out, cap)` copy API (no dangling pointer)
- [x] Swift bridge fully rewritten against ABI 2; all call sites updated

### Gate
- [ ] `scripts/test_core.sh` green locally
- [ ] `.github/workflows/core.yml` green (compile, analyze, verify, leak)
- [ ] `.github/workflows/ios.yml` green (SwiftLint + unsigned IPA)

## Anti-spiral protocol

1. One workstream active at a time. Anything else is a **logged blocker**, not
   a side quest.
2. Start every session at **Next action**; end every session by appending to the
   **Progress log** and updating the status board.
3. No re-planning mid-workstream. Scope changes go in "Decisions" below and are
   dated.
4. No deleting tests, weakening assertions, adding suppressions, or widening a
   check to get green. Fix the cause.
5. Every gate is run twice: once locally, once in CI. CI is authoritative.
6. Pushing is allowed only when the local gate is green; CI failures are fixed
   forward in the same workstream.
7. No platform, build system, dependency or asset pipeline is added without an
   explicit dated decision on this page.

## Decisions

| Date | Decision |
|---|---|
| 2026-11-11 | iOS is the only shipping platform. Host tests live in `tests/` and are CI tooling, not a platform target. |
| 2026-11-11 | ABI 2 is the boundary contract. Frame reads are pointer-based and allocation-free; strings are cached by revision. |
| 2026-11-11 | Account/save/sync logic moves into portable C++ behind an injectable transport in WS1; Swift keeps Keychain + UI only. |

## Progress log

| Date | WS | Change | Gate | Next |
|---|---|---|---|---|
| 2026-11-11 | WS0 | Purged desktop/CMake scaffolding, rewrote ABI + Swift bridge, added tracker and split CI | local compile pending | run `scripts/test_core.sh`, push, watch CI |

## Definition of done

1. `clang++`/`g++ -Wall -Wextra -Werror` clean on every TU; ASan/valgrind clean.
2. Determinism, content-validation, save-migration and economy suites pass.
3. Scripted bot completes campaign, Spire, fishing/animal sweeps, base, ship,
   1,000 planets, duels and market with no invariant violation.
4. Server API suite passes against Postgres including isolation and conflicts.
5. iOS target builds an unsigned IPA with shaders compiled; SwiftLint strict.
6. Offscreen render regression passes; no stub remains (grep-enforced).

Not verifiable from this environment: real-device frame rate, App Store
signing, and on-device visuals. These are reported as limitations, never as
passes.
