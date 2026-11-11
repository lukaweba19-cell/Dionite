# Architecture Updates Required

These systems need to be added to the existing Dionite codebase.

## New Major Modules

### World
- Expanded biome definitions + water body system
- BuildingSystem (piece placement, claiming, damage, blueprints)
- VehicleSystem + Boat physics
- WildlifeManager + TrackingSystem

### Lifestyle
- FishingSystem (rod stats, bite logic, mini-game, fish database)
- PetSystem (collection, bonding, active/passive slots, bonuses)
- HuntingSystem

### Combat Extensions
- Expanded WeaponFamily support
- CharmSystem
- SpecialPerkSystem (with weighted random rolls + reroll)
- DuelSystem (challenge, accept, no-loss rewards)

### Data
- Massive fish database (300+ entries with models, sell values, preferences)
- Animal database
- Pet database
- Building piece definitions
- Vehicle definitions

### UI
- Building mode UI
- Fishing HUD + tension mini-game
- Pet management screen
- Duel prompt + results
- Vehicle controls

### Networking
- Building ownership & sync
- Duel state machine
- Shared world events
- Vehicle passenger sync (later)

The existing ECS, StreamingManager, Loot, Progression, and Combat systems provide an excellent foundation. Most of the new systems can live as additional modules under `src/World/`, `src/Lifestyle/`, and extensions to `src/Combat/`.