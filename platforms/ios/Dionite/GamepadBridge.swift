// ============================================================================
// Dionite — GamepadBridge: GameController framework → core.
// Supports Xbox / PlayStation / MFi controllers via GCController.
//
// Mapping
//   Right trigger / A   fire
//   B                   dash
//   X, Y, LB, RB        ability slots 0-3
//   D-pad left / right  ability slots 4-5
//   D-pad up            interact (open chest / take portal)
//   D-pad down          potion
//   Left stick          move      Right stick  aim
//   Right trigger held  camera yaw with the right stick pressed
// ============================================================================
import Foundation
import GameController

final class GamepadBridge {
    static let shared = GamepadBridge()
    private init() {}

    private var controller: GCController?

    func start() {
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(controllerConnected),
                                               name: .GCControllerDidConnect,
                                               object: nil)
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(controllerDisconnected),
                                               name: .GCControllerDidDisconnect,
                                               object: nil)
        GCController.startWirelessControllerDiscovery {}
        if let first = GCController.controllers().first { attach(first) }
    }

    @objc private func controllerConnected(_ note: Notification) {
        if let connected = note.object as? GCController { attach(connected) }
    }

    @objc private func controllerDisconnected(_ note: Notification) {
        controller = nil
    }

    private func attach(_ controller: GCController) {
        self.controller = controller
        guard let pad = controller.extendedGamepad else { return }

        pad.buttonA.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setFire(pressed)
        }
        pad.buttonB.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setDash(pressed)
        }
        pad.buttonX.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(0, pressed)
        }
        pad.buttonY.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(1, pressed)
        }
        pad.leftShoulder.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(2, pressed)
        }
        pad.rightShoulder.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(3, pressed)
        }
        pad.dpad.left.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(4, pressed)
        }
        pad.dpad.right.valueChangedHandler = { [weak self] _, _, pressed in
            self?.bridge.setAbility(5, pressed)
        }
        pad.dpad.up.valueChangedHandler = { [weak self] _, _, pressed in
            if pressed { self?.bridge.interact() }
        }
        pad.dpad.down.valueChangedHandler = { [weak self] _, _, pressed in
            if pressed { self?.bridge.usePotion() }
        }
    }

    private var bridge: DioniteBridge { return DioniteBridge.shared }

    func pumpToCore() {
        guard let pad = controller?.extendedGamepad else { return }
        bridge.setMove(x: pad.leftThumbstick.xAxis.value,
                       y: -pad.leftThumbstick.yAxis.value)
        bridge.setAim(x: pad.rightThumbstick.xAxis.value,
                      y: -pad.rightThumbstick.yAxis.value)
        bridge.setFire(pad.rightTrigger.isPressed || pad.buttonA.isPressed)
    }
}
