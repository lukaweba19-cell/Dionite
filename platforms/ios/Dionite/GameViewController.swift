// ============================================================================
// Dionite — GameViewController.
//
// Owns the MTKView render loop, the touch/gamepad input plumbing and the HUD
// overlay. All simulation lives in the C++ core; this class only forwards
// input, pulls the per-frame snapshot and draws it.
// ============================================================================
import UIKit
import MetalKit
import simd

final class GameViewController: UIViewController, MTKViewDelegate {

    private var metalView: MTKView!
    private var commandQueue: MTLCommandQueue!
    private var renderer: GameRenderer?

    private let bridge = DioniteBridge.shared
    private let audio = AudioEngine.shared
    private let hud = HUDView(frame: .zero)
    private let menu = GameMenuView(frame: .zero)
    private var moveStick: VirtualJoystick!
    private var classSelect: ClassSelectView?

    private var lastFrameTime: CFTimeInterval = 0
    private var booted = false
    private var saveDir = ""
    private var lastHud = DIHud()

    // MARK: - Lifecycle

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = Theme.void

        guard let device = MTLCreateSystemDefaultDevice() else { return }
        metalView = MTKView(frame: view.bounds, device: device)
        metalView.colorPixelFormat = .bgra8Unorm
        metalView.depthStencilPixelFormat = .depth32Float
        metalView.clearColor = MTLClearColor(red: 0.02, green: 0.02, blue: 0.03, alpha: 1)
        metalView.preferredFramesPerSecond = 60
        metalView.isMultipleTouchEnabled = true
        metalView.delegate = self
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(metalView)

        commandQueue = device.makeCommandQueue()
        renderer = GameRenderer(device: device,
                                pixelFormat: metalView.colorPixelFormat,
                                depthFormat: metalView.depthStencilPixelFormat)

        setupControls()
        setupGestures()
        GamepadBridge.shared.start()
        audio.start()

        NotificationCenter.default.addObserver(self,
                                               selector: #selector(enterBackground),
                                               name: UIApplication.didEnterBackgroundNotification,
                                               object: nil)
        NotificationCenter.default.addObserver(self,
                                               selector: #selector(enterForeground),
                                               name: UIApplication.willEnterForegroundNotification,
                                               object: nil)

        lastFrameTime = CACurrentMediaTime()
        beginSession()
    }

    deinit {
        audio.stopEngine()
    }

    @objc private func enterBackground() {
        audio.pauseEngine()
    }

    @objc private func enterForeground() {
        audio.resumeEngine()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        moveStick.frame = CGRect(x: 16, y: view.bounds.height - 154, width: 140, height: 140)
        hud.frame = view.bounds
        menu.frame = view.bounds
        classSelect?.frame = view.bounds
    }

    // MARK: - Session

    private func beginSession() {
        let documents = FileManager.default.urls(for: .documentDirectory,
                                                 in: .userDomainMask).first
        saveDir = documents?.path ?? NSTemporaryDirectory()
        if saveDir.hasSuffix("/") == false { saveDir += "/" }

        if DioniteBridge.saveExists(in: saveDir) {
            boot(classId: 0, hasSave: true)
        } else {
            let picker = ClassSelectView(frame: view.bounds)
            picker.autoresizingMask = [.flexibleWidth, .flexibleHeight]
            picker.onSelect = { [weak self] chosen in
                self?.boot(classId: chosen, hasSave: false)
            }
            view.addSubview(picker)
            classSelect = picker
        }
    }

    private func boot(classId: Int32, hasSave: Bool) {
        classSelect?.removeFromSuperview()
        classSelect = nil
        bridge.boot(saveDir: saveDir, classId: classId, seed: 0, hasSave: hasSave)
        bridge.resize(width: Int32(max(1, metalView.drawableSize.width)),
                      height: Int32(max(1, metalView.drawableSize.height)))
        booted = true
        lastFrameTime = CACurrentMediaTime()
    }

    // MARK: - Controls

    private func setupControls() {
        moveStick = VirtualJoystick(frame: CGRect(x: 16, y: 0, width: 140, height: 140))
        moveStick.onChange = { [weak self] dx, dy in
            self?.bridge.setMove(x: Float(dx), y: Float(-dy))
        }
        view.addSubview(moveStick)
        view.addSubview(hud)
        view.addSubview(menu)

        hud.onAbility = { [weak self] slot, _ in
            guard let self = self else { return }
            self.bridge.setAbility(slot, true)
            DispatchQueue.main.async { self.bridge.setAbility(slot, false) }
        }
        hud.onFire = { [weak self] pressed in self?.bridge.setFire(pressed) }
        hud.onDash = { [weak self] pressed in self?.bridge.setDash(pressed) }
        hud.onPotion = { [weak self] in self?.bridge.usePotion() }
        hud.onInteract = { [weak self] in self?.bridge.interact() }
        hud.onRespawn = { [weak self] in self?.bridge.respawn() }
        hud.onMenu = { [weak self] in
            guard let self = self else { return }
            self.menu.present(hud: self.lastHud, spireBest: self.bridge.spireFloorBest)
        }

        menu.onClose = { [weak self] in self?.menu.isHidden = true }
        menu.onEquip = { [weak self] index in
            self?.bridge.equipItem(at: index)
            self?.menu.isHidden = true
        }
        menu.onTravel = { [weak self] region in
            self?.bridge.travel(to: region)
            self?.menu.isHidden = true
        }
        menu.onStartSpire = { [weak self] in
            self?.bridge.startSpire()
            self?.menu.isHidden = true
        }
        menu.onExitSpire = { [weak self] in
            self?.bridge.exitSpire()
            self?.menu.isHidden = true
        }
        menu.onSave = { [weak self] in self?.bridge.saveNow() }
    }

    private func setupGestures() {
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        metalView.addGestureRecognizer(tap)

        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.minimumNumberOfTouches = 2
        pan.maximumNumberOfTouches = 2
        metalView.addGestureRecognizer(pan)
    }

    /// A world tap becomes a click-to-move destination.
    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        guard booted else { return }
        let point = recognizer.location(in: metalView)
        let width = max(1.0, metalView.bounds.width)
        let height = max(1.0, metalView.bounds.height)
        let ndcX = Float(point.x / width * 2 - 1)
        let ndcY = Float(1 - point.y / height * 2)
        if let world = bridge.screenToWorld(ndcX: ndcX, ndcY: ndcY) {
            bridge.clickToMove(world: world)
        }
    }

    /// Two-finger drag rotates the camera the way Diablo IV does.
    @objc private func handlePan(_ recognizer: UIPanGestureRecognizer) {
        guard recognizer.numberOfTouches >= 2 else { return }
        let translation = recognizer.translation(in: metalView)
        bridge.cameraPan(deltaDeg: Float(translation.x) * 0.09)
        recognizer.setTranslation(.zero, in: metalView)
    }

    // MARK: - Frame

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        bridge.resize(width: Int32(size.width), height: Int32(size.height))
    }

    func draw(in view: MTKView) {
        let now = CACurrentMediaTime()
        var delta = Float(now - lastFrameTime)
        lastFrameTime = now
        if delta <= 0 { delta = 1.0 / 60.0 }
        if delta > 0.1 { delta = 0.1 }

        if booted {
            GamepadBridge.shared.pumpToCore()
            bridge.tick(delta: delta)
            audio.pumpFromCore()
        }

        guard let drawable = view.currentDrawable,
              let passDescriptor = view.currentRenderPassDescriptor,
              let commandBuffer = commandQueue.makeCommandBuffer() else {
            return
        }

        var scene = SceneUniforms()
        if booted {
            _ = bridge.cameraSnapshot()
            scene = buildScene()
        }

        passDescriptor.colorAttachments[0].loadAction = .clear
        passDescriptor.colorAttachments[0].clearColor =
            MTLClearColor(red: Double(scene.fog.x) * 0.45,
                          green: Double(scene.fog.y) * 0.45,
                          blue: Double(scene.fog.z) * 0.55,
                          alpha: 1)
        passDescriptor.depthAttachment.loadAction = .clear
        passDescriptor.depthAttachment.clearDepth = 1.0

        if let encoder = commandBuffer.makeRenderCommandEncoder(descriptor: passDescriptor) {
            if booted, let renderer = renderer {
                let snapshot = bridge.instances()
                renderer.encode(encoder,
                                scene: &scene,
                                instances: snapshot.pointer,
                                opaque: snapshot.opaque,
                                translucent: snapshot.translucent,
                                additive: snapshot.additive)
            }
            encoder.endEncoding()
        }
        commandBuffer.present(drawable)
        commandBuffer.commit()

        refreshUI(scene: scene, drawableSize: view.drawableSize)
    }

    private func buildScene() -> SceneUniforms {
        var scene = SceneUniforms()
        let matrix = bridge.viewProjection()
        scene.viewProj = simd_float4x4(columns: (
            SIMD4<Float>(matrix[0], matrix[1], matrix[2], matrix[3]),
            SIMD4<Float>(matrix[4], matrix[5], matrix[6], matrix[7]),
            SIMD4<Float>(matrix[8], matrix[9], matrix[10], matrix[11]),
            SIMD4<Float>(matrix[12], matrix[13], matrix[14], matrix[15])
        ))

        let vectors = bridge.cameraVectors()
        let factors = bridge.cameraFactors()
        let basis = bridge.cameraBasis()

        scene.eye = SIMD4(vectors[0], vectors[1], vectors[2], factors[0])
        scene.sun = SIMD4(vectors[3], vectors[4], vectors[5], factors[1])
        scene.sunColor = SIMD4(vectors[6], vectors[7], vectors[8], factors[2])
        scene.fog = SIMD4(vectors[9], vectors[10], vectors[11], factors[3])
        scene.post = SIMD4(factors[4], factors[5], 0, 0)
        scene.camRight = SIMD4(basis.right.x, basis.right.y, basis.right.z, 0)
        scene.camUp = SIMD4(basis.up.x, basis.up.y, basis.up.z, 0)
        return scene
    }

    private func refreshUI(scene: SceneUniforms, drawableSize: CGSize) {
        guard booted else { return }
        if let snapshot = bridge.hudSnapshot() {
            lastHud = snapshot
            hud.update(hud: snapshot)
        }

        var queued = 0
        while queued < 4, let event = bridge.popEvent() {
            hud.show(event: event)
            queued += 1
        }

        let numbers = bridge.damageNumbers()
        let size = CGSize(width: max(1, view.bounds.width),
                          height: max(1, view.bounds.height))
        hud.show(damage: numbers, viewProj: scene.viewProj, size: size)
    }
}

// MARK: - Class select

/// Shown on first launch so the player picks the hero the campaign is built
/// around. Choosing one boots the core with that class.
final class ClassSelectView: UIView {
    var onSelect: ((Int32) -> Void)?

    private struct Entry {
        let id: Int32
        let name: String
        let tagline: String
        let resource: String
        let tint: UIColor
    }

    private let entries: [Entry] = [
        Entry(id: 0, name: "Crusader", tagline: "Bulwark of the Forgotten Light",
              resource: "Wrath", tint: UIColor(red: 0.98, green: 0.45, blue: 0.09, alpha: 1)),
        Entry(id: 1, name: "Necromancer", tagline: "Shepherd of the Pale Court",
              resource: "Essence", tint: UIColor(red: 0.66, green: 0.33, blue: 0.97, alpha: 1)),
        Entry(id: 2, name: "Sorcerer", tagline: "Storm-touched Heretic",
              resource: "Mana", tint: UIColor(red: 0.23, green: 0.51, blue: 0.96, alpha: 1)),
        Entry(id: 3, name: "Ranger", tagline: "Whisper of the Verdant",
              resource: "Discipline", tint: UIColor(red: 0.09, green: 0.64, blue: 0.29, alpha: 1)),
        Entry(id: 4, name: "Monk", tagline: "Wanderer of the Frozen Spire",
              resource: "Spirit", tint: UIColor(red: 0.22, green: 0.74, blue: 0.97, alpha: 1))
    ]

    private let titleLabel = UILabel()
    private let subtitleLabel = UILabel()
    private var buttons: [UIButton] = []

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = Theme.void

        titleLabel.text = "SHATTERED WILDS"
        titleLabel.font = Theme.display(34)
        titleLabel.textColor = Theme.goldBright
        titleLabel.textAlignment = .center
        addSubview(titleLabel)

        subtitleLabel.text = "Choose the hero who will walk the five realms"
        subtitleLabel.font = Theme.body(17)
        subtitleLabel.textColor = Theme.textSecondary
        subtitleLabel.textAlignment = .center
        addSubview(subtitleLabel)

        for entry in entries {
            let button = UIButton(type: .system)
            button.tag = Int(entry.id)
            button.setTitle(entry.name.uppercased(), for: .normal)
            button.titleLabel?.font = Theme.display(20)
            button.setTitleColor(Theme.textPrimary, for: .normal)
            button.backgroundColor = Theme.panel
            button.layer.borderColor = entry.tint.cgColor
            button.layer.borderWidth = 1.5
            button.layer.cornerRadius = 8
            button.addTarget(self, action: #selector(chosen(_:)), for: .touchUpInside)
            addSubview(button)
            buttons.append(button)

            let detail = UILabel()
            detail.text = "\(entry.tagline)  ·  \(entry.resource)"
            detail.font = Theme.body(13)
            detail.textColor = Theme.textSecondary
            detail.textAlignment = .center
            detail.tag = 1000 + Int(entry.id)
            addSubview(detail)
        }
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = min(760, bounds.width - 40)
        let originX = (bounds.width - width) / 2
        titleLabel.frame = CGRect(x: originX, y: bounds.height * 0.12,
                                  width: width, height: 46)
        subtitleLabel.frame = CGRect(x: originX, y: bounds.height * 0.12 + 48,
                                     width: width, height: 26)

        let cardHeight: CGFloat = 58
        let gap: CGFloat = 10
        var y = bounds.height * 0.30
        for (index, button) in buttons.enumerated() {
            button.frame = CGRect(x: originX, y: y, width: width, height: cardHeight)
            if let detail = viewWithTag(1000 + index) {
                detail.frame = CGRect(x: originX, y: y + cardHeight + 2,
                                      width: width, height: 18)
            }
            y += cardHeight + gap + 22
        }
    }

    @objc private func chosen(_ sender: UIButton) {
        onSelect?(Int32(sender.tag))
    }
}
