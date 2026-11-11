#!/usr/bin/env sh
# ============================================================================
# Dionite — host-side core verification gate.
#
# Compiles the headless verification harness against the portable game core and
# runs it. This is CI tooling, not a platform target: it proves the simulation,
# save/load, campaign loop and snapshot ABI behave before the iOS build runs.
# ============================================================================
set -eu
cd "$(dirname "$0")/.."

CXX=${CXX:-g++}
OUT=build/dionite_verify
mkdir -p build

$CXX -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers \
    -Isrc -Isrc/external \
    tests/core_verify.cpp \
    src/Game/GameRuntime.cpp \
    src/Combat/Weapons/WeaponBase.cpp \
    src/Loot/Items/ItemBase.cpp \
    src/Audio/AudioManager.cpp \
    src/Progression/Skills/SkillLibrary.Crusader.cpp \
    src/Progression/Skills/SkillLibrary.Necromancer.cpp \
    src/Progression/Skills/SkillLibrary.Sorcerer.cpp \
    src/Progression/Skills/SkillLibrary.Ranger.cpp \
    src/Progression/Skills/SkillLibrary.Monk.cpp \
    -o "$OUT"

"$OUT"
