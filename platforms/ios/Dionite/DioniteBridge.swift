// ============================================================================
// Dionite — Swift ↔ C++ bridge.
//
// The C symbols arrive through Dionite-Bridging-Header.h. Struct members that
// are C fixed-size arrays (matrix columns, HUD strings) are read through the
// buffer-based accessors in DioniteAPI.h rather than directly, because those
// do not import into Swift in a usable shape. Scalar struct members are read
// straight off the imported struct.
// ============================================================================
import Foundation

// MARK: - Helpers

/// Reads a NUL-terminated string out of a zero-filled `CChar` array.
private func readCString(_ buffer: [CChar]) -> String {
    return buffer.withUnsafeBufferPointer { pointer in
        guard let base = pointer.baseAddress else { return "" }
        return String(cString: base)
    }
}

// MARK: - Models surfaced to the UI

struct AbilityState {
    var name: String = ""
    var cooldown: Float = 0
    var cooldownMax: Float = 0
    var ready: Bool = false
    var cost: Float = 0
}

struct ItemRow {
    var name: String
    var detail: String
    var rarity: Int32
    var ilvl: Int32
    var damage: Float
    var equipped: Bool
    var sockets: Int32
}

struct WorldEvent {
    var kind: Int32
    var text: String
}

// MARK: - Bridge

final class DioniteBridge {
    static let shared = DioniteBridge()
    private init() {}

    // MARK: Lifecycle

    func boot(saveDir: String, classId: Int32, seed: UInt64, hasSave: Bool) {
        dionite_boot(saveDir, classId, seed, hasSave ? 1 : 0)
    }

    func shutdown() { dionite_shutdown() }
    func tick(delta: Float) { dionite_tick(delta) }
    func resize(width: Int32, height: Int32) { dionite_resize(width, height) }
    func pause() { dionite_pause() }
    func resume() { dionite_resume() }
    func saveNow() { dionite_save_now() }
    var version: Int32 { dionite_version() }

    static func saveExists(in directory: String) -> Bool {
        return dionite_save_exists(directory) != 0
    }

    // MARK: Input

    func setMove(x: Float, y: Float) { dionite_set_move(x, y) }
    func setAim(x: Float, y: Float) { dionite_set_aim(x, y) }
    func setFire(_ pressed: Bool) { dionite_set_fire(pressed ? 1 : 0) }
    func setDash(_ pressed: Bool) { dionite_set_dash(pressed ? 1 : 0) }
    func setAbility(_ slot: Int, _ pressed: Bool) {
        dionite_set_ability(Int32(slot), pressed ? 1 : 0)
    }
    func clickToMove(world: SIMD3<Float>) { dionite_click_to_move(world.x, world.y, world.z) }
    func cameraPan(deltaDeg: Float) { dionite_camera_pan(deltaDeg) }

    /// Projects a screen tap (NDC, y up) onto the ground plane.
    func screenToWorld(ndcX: Float, ndcY: Float) -> SIMD3<Float>? {
        var world = [Float](repeating: 0, count: 3)
        let ok = world.withUnsafeMutableBufferPointer { pointer -> Int32 in
            dionite_screen_to_world(ndcX, ndcY, pointer.baseAddress)
        }
        guard ok != 0 else { return nil }
        return SIMD3(world[0], world[1], world[2])
    }

    /// `[right.xyz, up.xyz]` of the camera, for billboarding.
    func cameraBasis() -> (right: SIMD3<Float>, up: SIMD3<Float>) {
        var right = [Float](repeating: 0, count: 3)
        var up = [Float](repeating: 0, count: 3)
        right.withUnsafeMutableBufferPointer { rightPointer in
            up.withUnsafeMutableBufferPointer { upPointer in
                dionite_camera_basis(rightPointer.baseAddress, upPointer.baseAddress)
            }
        }
        return (SIMD3(right[0], right[1], right[2]), SIMD3(up[0], up[1], up[2]))
    }
    func interact() { dionite_interact() }
    func usePotion() { dionite_use_potion() }

    // MARK: Game flow

    func respawn() { dionite_respawn() }
    func travel(to region: Int32) { dionite_travel(region) }
    func startSpire() { dionite_start_spire() }
    func exitSpire() { dionite_exit_spire() }
    func equipItem(at index: Int32) { dionite_equip_item(index) }

    // MARK: Frame snapshots

    /// Refreshes the cached camera block and returns the raw struct.
    func cameraSnapshot() -> DICamera? {
        var camera = DICamera()
        return dionite_camera(&camera) != 0 ? camera : nil
    }

    /// Refreshes the HUD block and returns the raw struct (scalars only).
    func hudSnapshot() -> DIHud? {
        var hud = DIHud()
        return dionite_hud(&hud) != 0 ? hud : nil
    }

    /// 16 column-major floats forming `proj * view`.
    func viewProjection() -> [Float] {
        guard let pointer = dionite_camera_view_proj() else {
            return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        }
        return (0..<16).map { pointer[$0] }
    }

    /// `[eye.xyz, sunDirection.xyz, sunColor.xyz, fogColor.xyz]`
    func cameraVectors() -> [Float] {
        var values = [Float](repeating: 0, count: 12)
        dionite_camera_vectors(&values)
        return values
    }

    /// `[time, sunIntensity, ambient, fogDensity, exposure, shake]`
    func cameraFactors() -> [Float] {
        var values = [Float](repeating: 0, count: 6)
        dionite_camera_factors(&values)
        return values
    }

    func instances() -> (pointer: UnsafePointer<DIInstance>?, opaque: Int32,
                         translucent: Int32, additive: Int32) {
        var opaqueCount: Int32 = 0
        var translucentCount: Int32 = 0
        var additiveCount: Int32 = 0
        let pointer = dionite_instances(&opaqueCount, &translucentCount, &additiveCount)
        return (pointer, opaqueCount, translucentCount, additiveCount)
    }

    // MARK: Strings and per-slot data

    func hudText(_ field: Int32) -> String {
        var buffer = [CChar](repeating: 0, count: 160)
        buffer.withUnsafeMutableBufferPointer { pointer in
            dionite_hud_text(field, pointer.baseAddress, Int32(pointer.count))
        }
        return readCString(buffer)
    }

    func ability(_ slot: Int) -> AbilityState {
        var nameBuffer = [CChar](repeating: 0, count: 32)
        var cooldown: Float = 0
        var cooldownMax: Float = 0
        var ready: UInt32 = 0
        var cost: Float = 0
        nameBuffer.withUnsafeMutableBufferPointer { pointer in
            dionite_ability(Int32(slot), pointer.baseAddress, Int32(pointer.count),
                            &cooldown, &cooldownMax, &ready, &cost)
        }
        return AbilityState(name: readCString(nameBuffer),
                            cooldown: cooldown,
                            cooldownMax: cooldownMax,
                            ready: ready != 0,
                            cost: cost)
    }

    func popEvent() -> WorldEvent? {
        var kind: Int32 = 0
        var buffer = [CChar](repeating: 0, count: 192)
        var written = 0
        buffer.withUnsafeMutableBufferPointer { pointer in
            written = Int(dionite_pop_event_text(&kind, pointer.baseAddress,
                                                 Int32(pointer.count)))
        }
        guard written != 0 else { return nil }
        let text = readCString(buffer)
        guard !text.isEmpty else { return nil }
        return WorldEvent(kind: kind, text: text)
    }

    func damageNumbers(limit: Int = 64) -> [DIDamageNumber] {
        var buffer = [DIDamageNumber](repeating: DIDamageNumber(), count: limit)
        let count = buffer.withUnsafeMutableBufferPointer { pointer -> Int32 in
            dionite_damage_numbers(pointer.baseAddress, Int32(pointer.count))
        }
        guard count > 0 else { return [] }
        return Array(buffer.prefix(Int(count)))
    }

    // MARK: Inventory

    var inventoryCount: Int32 { return dionite_inventory_count() }

    func inventoryRow(_ index: Int32) -> ItemRow? {
        var nameBuffer = [CChar](repeating: 0, count: 64)
        var detailBuffer = [CChar](repeating: 0, count: 128)
        var rarity: Int32 = 0
        var ilvl: Int32 = 0
        var damage: Float = 0
        var equipped: Int32 = 0
        var sockets: Int32 = 0

        let written: Int32 = nameBuffer.withUnsafeMutableBufferPointer { namePointer in
            detailBuffer.withUnsafeMutableBufferPointer { detailPointer in
                dionite_inventory_row(index,
                                      namePointer.baseAddress, Int32(namePointer.count),
                                      detailPointer.baseAddress, Int32(detailPointer.count),
                                      &rarity, &ilvl, &damage, &equipped, &sockets)
            }
        }
        guard written != 0 else { return nil }
        return ItemRow(name: readCString(nameBuffer),
                       detail: readCString(detailBuffer),
                       rarity: rarity,
                       ilvl: ilvl,
                       damage: damage,
                       equipped: equipped != 0,
                       sockets: sockets)
    }

    // MARK: Status

    var skillPoints: Int32 { return dionite_skill_points() }
    var spireFloorBest: Int32 { return dionite_spire_best() }
    var playSeconds: Float { return dionite_play_seconds() }
    var campaignComplete: Bool { return dionite_campaign_complete() != 0 }
    var resumedSave: Bool { return dionite_has_save() != 0 }
}
