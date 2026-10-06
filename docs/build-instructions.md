# Dionite — Build Instructions

> The **C++ core compiles and is verified on Linux/macOS** with nothing but
> `g++` (C++17) — no package installs required (nlohmann/json is vendored at
> `src/external/`). The **iOS app** needs macOS — or just CI: every push runs
> the full gate suite and uploads an unsigned `.ipa` artifact.

## Prerequisites

| Tool                | Version | Notes                                          |
| ------------------- | ------- | ---------------------------------------------- |
| g++ or Clang        | C++17   | `apt install g++` / Xcode CLT                  |
| CMake               | ≥ 3.20  | optional — desktop harness & libs only          |
| Xcode               | 15+     | iOS build (Mac required)                       |
| XcodeGen            | latest  | `brew install xcodegen` (generates the .xcodeproj) |
| SwiftLint           | latest  | optional locally; preinstalled on CI runners   |
| Node.js             | 20+     | for the backend / admin dashboard              |
| Docker              | 24+     | optional, for backend + Postgres stack         |

nlohmann/json v3.11.3 is **vendored** — no vcpkg/brew install needed.

## 1. C++ core — compile check + campaign verifier (any OS)

```bash
# Full-tree compile, warnings are errors (same gate CI runs):
files=$(find src -name '*.cpp' | sort)
files="$files platforms/ios/Dionite/DioniteBridgeImpl.cpp"
g++ -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -Werror \
  -Isrc -Isrc/external -Iplatforms/ios/Dionite -fsyntax-only $files

# Campaign verifier — 83 checks over the full game loop (boot, 20 campaign
# floors, bosses, loot, quests, fast travel, Infinity Spire, save/load):
g++ -std=c++17 -O1 -Isrc -Isrc/external \
  src/platforms/desktop/verify_campaign.cpp src/Game/GameRuntime.cpp \
  src/Combat/Weapons/WeaponBase.cpp src/Loot/Items/ItemBase.cpp \
  src/Audio/AudioManager.cpp \
  src/Progression/Skills/SkillLibrary.*.cpp -o dionite_verify
./dionite_verify        # "83 checks, 0 failures", exit 0

# Optional static + leak analysis (what CI enforces):
g++ -std=c++17 -fanalyzer -Wall -Wextra -Werror \
  -Isrc -isystem src/external -c src/Game/GameRuntime.cpp -o /dev/null
valgrind --leak-check=full --errors-for-leak-kinds=definite ./dionite_verify
```

CMake equivalents:

```bash
cmake -S . -B build -DDIONITE_USE_BUNDLED=ON
cmake --build build -j
./build/DioniteVerify    # 81-check campaign verifier
./build/DioniteDesktop   # legacy headless loop
```

## 2. iOS build — unsigned `.ipa` (Mac, or CI)

The Xcode project is generated from `project.yml` (XcodeGen); it compiles the
entire C++ tree plus the Swift/Metal host and bundles `assets/`.

```bash
brew install xcodegen
sh ./scripts/package_ipa.sh        # -> dist/Dionite.ipa (+ dist/symbols/)
```

The script:

1. `xcodegen generate` → `Dionite.xcodeproj` (git-ignored, regenerated any time)
2. `xcodebuild` for a generic iOS device with **signing disabled**
3. zips `Payload/Dionite.app` into `dist/Dionite.ipa`

The `.ipa` is **unsigned/ad-hoc** — sideload it with AltStore, TrollStore, or
Sideloadly. To run from Xcode on a device, open `Dionite.xcodeproj`, select
your team under *Signing & Capabilities*, and run.

To sign for App Store / TestFlight: set your team, archive, and distribute as
usual — `PRODUCT_BUNDLE_IDENTIFIER` is `com.dionite.shatteredwilds`.

## 3. Continuous integration (`.github/workflows/ios-build.yml`)

Every push runs three parallel jobs; all must be green:

| Job         | Runner     | Gates                                                                 |
| ----------- | ---------- | --------------------------------------------------------------------- |
| `cpp-checks`| ubuntu     | `-Werror` compile of every TU, GCC `-fanalyzer` static/leak analysis, the 81-check campaign verifier, valgrind leak check over a full campaign run |
| `swiftlint` | macos-14   | `swiftlint lint --strict` over the Swift/Metal host                    |
| `build-ipa` | macos-14   | XcodeGen + unsigned `xcodebuild`, uploads **`Dionite.ipa`** artifact (+ dSYM; xcodebuild log uploaded on failure) |

Download the `.ipa` from the workflow run's **Artifacts** section. Nothing to
configure: no certificates, no provisioning profiles, no secrets.

## 4. Android build (Studio + NDK)

Open `platforms/android` in Android Studio (or run `./gradlew assembleRelease`).
The `MainActivity.java` JNI methods call into the C++ library compiled by
CMake at the repo root.

## 5. Backend & Admin Dashboard

```bash
cd server
docker compose up --build       # starts Postgres + server (:4000) + admin (:5173)
```
Or run server only:
```bash
cd server
yarn install
DATABASE_URL=postgres://dionite:dionite@localhost:5432/dionite \
JWT_SECRET=change-me \
yarn dev
```
Admin only:
```bash
cd server/web
yarn install
VITE_API_URL=http://localhost:4000 yarn dev
```
Default admin (seeded only if you POST `/api/auth/register` with
`email=admin@dionite.game` then bump role manually via SQL or create your own).

## 6. Asset Pipeline

Meshes, textures and audio are **generated procedurally** at runtime by the
iOS host (`GameRenderer.swift` mesh factory + vertex-colored materials), so a
shipped build needs no binary art assets.

Drop `.fbx` / `.gltf` into `assets/models/` when you wire up real art; the
runtime supports `assimp`-loaded meshes (link `find_package(assimp)` in
`CMakeLists.txt` under `DioniteGame` once you wire up the import path).

Audio: `.wav` / `.ogg` go in `assets/audio/{music,sfx,voice}/`.

Data: extend `assets/data/*.json` for new runes, gems, biomes, bosses.
