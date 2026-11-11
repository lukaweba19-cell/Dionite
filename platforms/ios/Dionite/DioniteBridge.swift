// ============================================================================
// Dionite — Swift ↔ C++ bridge (ABI 2).
//
// The only place Swift touches the game core. Per frame the host makes exactly
// two crossings: `dionite_tick()` and `dionite_frame()`. Everything in the frame
// snapshot is a pointer into runtime-owned storage, so reading it allocates
// nothing. Strings are pulled through stable `const char *` accessors and cached
// against `dionite_text_revision()` instead of being rebuilt every frame.
// ============================================================================
import Foundation
import simd

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

/// One rendered frame. The pointers are valid until the next tick/frame call,
/// so this value must not be stored across frames.
struct FrameSnapshot {
    var camera: DICamera
    var hud: DIHud
    var instances: UnsafePointer<DIInstance>?
    var opaqueCount: Int32
    var translucentCount: Int32
    var additiveCount: Int32
    var damageNumbers: UnsafeBufferPointer<DIDamageNumber>
    var viewProjection: simd_float4x4
}

// MARK: - Bridge

final class DioniteBridge {
    static let shared = DioniteBridge()

    /// Latest HUD block, kept for the menu and cloud upload paths.
    private(set) var hud = DIHud()

    private var textRevision: UInt32 = .max
    private var textByField: [Int32: String] = [:]
    private var abilityNames: [String] = []
    private var abilityStates: [AbilityState] = []
    private var eventBuffer = [DIEvent](repeating: DIEvent(), count: 8)

    private init() {
        abilityNames = [String](repeating: "", count: Int(DI_MAX_ABILITIES))
        abilityStates = [AbilityState](repeating: AbilityState(),
                                       count: Int(DI_MAX_ABILITIES))
    }

    // MARK: Lifecycle

    @discardableResult
    func boot(saveDir: String, classId: Int32, seed: UInt64, hasSave: Bool,
              width: Int32, height: Int32) -> Bool {
        var params = DIBootParams()
        params.seed = seed
        params.classId = classId
        params.hasSave = hasSave ? 1 : 0
        params.width = max(1, width)
        params.height = max(1, height)
        let started = saveDir.withCString { directory -> Int32 in
            params.saveDir = directory
            return dionite_boot(&params)
        }
        if started != 0 {
            textRevision = .max
        }
        return started != 0
    }

    func shutdown() { dionite_shutdown() }
    func tick(delta: Float) { dionite_tick(delta) }
    func resize(width: Int32, height: Int32) { dionite_resize(width, height) }
    func setPaused(_ paused: Bool) { dionite_set_paused(paused ? 1 : 0) }
    func saveNow() { dionite_save() }

    var isRunning: Bool { dionite_is_running() != 0 }
    var abiVersion: Int32 { dionite_abi_version() }
    var saveVersion: Int32 { dionite_save_version() }
    var lastError: String {
        guard let pointer = dionite_last_error() else { return "" }
        return String(cString: pointer)
    }

    static func saveExists(in directory: String) -> Bool {
        return directory.withCString { dionite_save_exists($0) != 0 }
    }

    // MARK: Input

    func move(x: Float, y: Float) { dionite_move(x, y) }
    func aim(x: Float, y: Float) { dionite_aim(x, y) }
    func fire(_ held: Bool) { dionite_fire(held ? 1 : 0) }
    func dash() { dionite_dash() }
    func ability(_ slot: Int) { dionite_ability(Int32(slot)) }
    func moveTo(_ world: SIMD3<Float>) { dionite_move_to(world.x, world.y, world.z) }
    func cameraPan(deltaDeg: Float) { dionite_camera_pan(deltaDeg) }
    func interact() { dionite_interact() }
    func usePotion() { dionite_use_potion() }

    /// Projects a screen tap (NDC, y up) onto the ground plane.
    func screenToWorld(ndcX: Float, ndcY: Float) -> SIMD3<Float>? {
        var world = SIMD3<Float>.zero
        let ok = withUnsafeMutablePointer(to: &world) { pointer -> Int32 in
            pointer.withMemoryRebound(to: Float.self, capacity: 3) { floats in
                dionite_screen_to_world(ndcX, ndcY, floats)
            }
        }
        return ok != 0 ? world : nil
    }

    // MARK: Game flow

    func respawn() { dionite_respawn() }
    func travel(to region: Int32) { dionite_travel(region) }
    func spire(enter: Bool) { dionite_spire(enter ? 1 : 0) }
    func equipItem(at index: Int32) { dionite_equip(index) }

    // MARK: Frame

    /// Pulls the per-frame snapshot. This is the only place the HUD, camera,
    /// instance list and damage numbers are read.
    func frame() -> FrameSnapshot? {
        guard let raw = dionite_frame() else { return nil }
        let snapshot = raw.pointee
        guard let cameraPointer = snapshot.camera,
              let hudPointer = snapshot.hud else {
            return nil
        }

        refreshTextCache()

        hud = hudPointer.pointee
        refreshAbilityStates()

        var projection = matrix_identity_float4x4
        if let floats = snapshot.viewProjection {
            projection = simd_float4x4(columns: (
                SIMD4<Float>(floats[0], floats[1], floats[2], floats[3]),
                SIMD4<Float>(floats[4], floats[5], floats[6], floats[7]),
                SIMD4<Float>(floats[8], floats[9], floats[10], floats[11]),
                SIMD4<Float>(floats[12], floats[13], floats[14], floats[15])
            ))
        }

        var damage = UnsafeBufferPointer<DIDamageNumber>(start: nil, count: 0)
        if let numbers = snapshot.damageNumbers, snapshot.damageNumberCount > 0 {
            damage = UnsafeBufferPointer(start: numbers,
                                         count: Int(snapshot.damageNumberCount))
        }

        return FrameSnapshot(camera: cameraPointer.pointee,
                             hud: hudPointer.pointee,
                             instances: snapshot.instances,
                             opaqueCount: snapshot.opaqueCount,
                             translucentCount: snapshot.translucentCount,
                             additiveCount: snapshot.additiveCount,
                             damageNumbers: damage,
                             viewProjection: projection)
    }

    // MARK: Strings (cached by revision)

    func hudText(_ field: Int32) -> String {
        refreshTextCache()
        return textByField[field] ?? ""
    }

    func ability(_ slot: Int) -> AbilityState {
        guard slot >= 0, slot < abilityStates.count else { return AbilityState() }
        return abilityStates[slot]
    }

    private func refreshTextCache() {
        let revision = dionite_text_revision()
        guard revision != textRevision else { return }
        textRevision = revision

        var fields: [Int32: String] = [:]
        for field in Int32(0)..<7 {
            if let pointer = dionite_hud_text(field) {
                fields[field] = String(cString: pointer)
            }
        }
        textByField = fields

        var names: [String] = []
        names.reserveCapacity(Int(DI_MAX_ABILITIES))
        for slot in 0..<Int(DI_MAX_ABILITIES) {
            if let pointer = dionite_ability_name(Int32(slot)) {
                names.append(String(cString: pointer))
            } else {
                names.append("")
            }
        }
        abilityNames = names
    }

    private func refreshAbilityStates() {
        var source = hud
        var states: [AbilityState] = []
        states.reserveCapacity(Int(DI_MAX_ABILITIES))
        withUnsafeBytes(of: &source.abilities) { raw in
            let abilities = raw.bindMemory(to: DIHudAbility.self)
            for slot in 0..<Int(DI_MAX_ABILITIES) {
                let ability = abilities[slot]
                states.append(AbilityState(name: abilityNames[slot],
                                           cooldown: ability.cooldown,
                                           cooldownMax: ability.cooldownMax,
                                           ready: ability.ready != 0,
                                           cost: ability.resourceCost))
            }
        }
        abilityStates = states
    }

    // MARK: Events

    func popEvents(limit: Int = 4) -> [WorldEvent] {
        let capacity = min(limit, eventBuffer.count)
        guard capacity > 0 else { return [] }
        let count = eventBuffer.withUnsafeMutableBufferPointer { buffer -> Int32 in
            dionite_pop_events(buffer.baseAddress, Int32(capacity))
        }
        guard count > 0 else { return [] }

        var events: [WorldEvent] = []
        events.reserveCapacity(Int(count))
        for index in 0..<Int(count) {
            var event = eventBuffer[index]
            let text = string(from: &event.text, capacity: 160)
            if !text.isEmpty {
                events.append(WorldEvent(kind: event.type, text: text))
            }
        }
        return events
    }

    // MARK: Inventory

    var inventoryCount: Int32 { dionite_inventory_count() }

    func inventoryRow(_ index: Int32) -> ItemRow? {
        var item = DIItem()
        guard dionite_inventory_item(index, &item) != 0 else { return nil }
        let name = string(from: &item.name, capacity: 48)
        let detail = string(from: &item.detail, capacity: 112)
        return ItemRow(name: name,
                       detail: detail,
                       rarity: item.rarity,
                       ilvl: item.ilvl,
                       damage: item.damage,
                       equipped: item.equipped != 0,
                       sockets: item.sockets)
    }

    // MARK: Status

    var skillPoints: Int32 { dionite_skill_points() }
    var spireFloorBest: Int32 { dionite_spire_best() }
    var playSeconds: Float { dionite_play_seconds() }
    var campaignComplete: Bool { dionite_campaign_complete() != 0 }
    var resumedSave: Bool { dionite_resumed_save() != 0 }

    /// Serialised live save, copied out of the runtime.
    func saveJSON() -> Data? {
        let needed = dionite_save_json(nil, 0)
        guard needed > 0 else { return nil }
        var buffer = [CChar](repeating: 0, count: Int(needed) + 1)
        let written = buffer.withUnsafeMutableBufferPointer { pointer -> Int32 in
            dionite_save_json(pointer.baseAddress, Int32(pointer.count))
        }
        guard written > 0 else { return nil }
        return buffer.withUnsafeBytes { raw in
            Data(raw.prefix(Int(written)))
        }
    }

    // MARK: Helpers

    private func string<T>(from value: inout T, capacity: Int) -> String {
        return withUnsafePointer(to: &value) { pointer in
            pointer.withMemoryRebound(to: CChar.self, capacity: capacity) { characters in
                String(cString: characters)
            }
        }
    }
}
