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
    private var authView: AuthView?
    private var loadingView: UIView?
    private var lastUploadAt: CFTimeInterval = 0
    private var lastSpireBest: Int32 = -1

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

        if GameService.shared.isSignedIn {
            showLoading(text: "Restoring your save…")
            Task { [weak self] in
                await self?.restoreCloudThenContinue()
            }
        } else {
            showAuth()
        }
    }

    /// Signed-in launch: refresh the token, pull the cloud save and adopt it
    /// only when it is further along than the device copy, then fall through
    /// to the normal local-save / class-select flow. Any network failure
    /// just means offline play — sync resumes on the next launch.
    private func restoreCloudThenContinue() async {
        do {
            _ = try await GameService.shared.refreshSession()
            if let cloud = try? await GameService.shared.fetchSave() {
                let local = readLocalSave()
                if GameService.shared.shouldAdoptCloudSave(cloud: cloud, local: local) {
                    writeLocalSave(cloud)
                }
            }
        } catch {
            // Offline — the local save carries the session.
        }
        hideLoading()
        continueWithLocalSave()
    }

    private func continueWithLocalSave() {
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

    private func showAuth() {
        let auth = AuthView(frame: view.bounds)
        auth.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        auth.onDone = { [weak self] signedIn in
            guard let self = self else { return }
            self.authView?.removeFromSuperview()
            self.authView = nil
            if signedIn {
                self.showLoading(text: "Restoring your save…")
                Task { [weak self] in
                    await self?.restoreCloudThenContinue()
                }
            } else {
                self.continueWithLocalSave()
            }
        }
        view.addSubview(auth)
        authView = auth
    }

    private func showLoading(text: String) {
        guard loadingView == nil else { return }
        let overlay = UIView(frame: view.bounds)
        overlay.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        overlay.backgroundColor = Theme.void
        let label = UILabel(frame: overlay.bounds)
        label.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        label.text = text
        label.textAlignment = .center
        label.font = Theme.body(17)
        label.textColor = Theme.textSecondary
        overlay.addSubview(label)
        view.addSubview(overlay)
        loadingView = overlay
    }

    private func hideLoading() {
        loadingView?.removeFromSuperview()
        loadingView = nil
    }

    private var saveFileURL: URL {
        return URL(fileURLWithPath: saveDir + "dionite_save.json")
    }

    private func readLocalSave() -> Data? {
        return try? Data(contentsOf: saveFileURL)
    }

    private func writeLocalSave(_ data: Data) {
        try? data.write(to: saveFileURL, options: .atomic)
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
            pumpCloud(now: now)
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

    // MARK: - Cloud sync

    private static let biomeIds = ["verdant_wilds", "ashen_wastes", "sunken_crypts",
                                   "frozen_spire", "sky_citadel"]

    /// Periodic cloud upload + spire best-run reporting. Runs on the frame
    /// thread; the network itself always happens inside a Task.
    private func pumpCloud(now: CFTimeInterval) {
        guard booted, GameService.shared.isSignedIn else {
            lastSpireBest = -1
            return
        }

        let best = bridge.spireFloorBest
        if lastSpireBest < 0 {
            lastSpireBest = best
        } else if best > lastSpireBest {
            lastSpireBest = best
            let floor = Int(best)
            let seconds = Double(bridge.playSeconds)
            let region = Int(lastHud.region)
            let biome = GameViewController.biomeIds[min(4, max(0, region))]
            Task {
                _ = try? await GameService.shared.submitSpireRun(floor: floor,
                                                                 score: floor * 100,
                                                                 biome: biome,
                                                                 seconds: seconds)
            }
        }

        if now - lastUploadAt >= 30 {
            lastUploadAt = now
            GameService.shared.pushCurrentSave()
        }
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

// MARK: - Auth view

/// Sign-in / registration gate shown when no session exists yet. The game
/// stays fully playable offline, but an account is what makes saves follow
/// the player across devices once the Dionite server is deployed. The server
/// URL is editable here so the build can point at localhost, a LAN box or a
/// deployed host without recompiling.
final class AuthView: UIView, UITextFieldDelegate {
    var onDone: ((Bool) -> Void)?

    private let titleLabel = UILabel()
    private let blurbLabel = UILabel()
    private let emailField = UITextField()
    private let passwordField = UITextField()
    private let nameField = UITextField()
    private let serverField = UITextField()
    private let primaryButton = UIButton(type: .system)
    private let switchButton = UIButton(type: .system)
    private let offlineButton = UIButton(type: .system)
    private let statusLabel = UILabel()
    private var registerMode = false
    private var busy = false

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = Theme.void

        titleLabel.text = "SHATTERED WILDS"
        titleLabel.font = Theme.display(30)
        titleLabel.textColor = Theme.goldBright
        titleLabel.textAlignment = .center
        addSubview(titleLabel)

        blurbLabel.text = "Sign in so your heroes, saves and rankings follow you "
            + "across every device."
        blurbLabel.font = Theme.body(15)
        blurbLabel.textColor = Theme.textSecondary
        blurbLabel.textAlignment = .center
        blurbLabel.numberOfLines = 0
        addSubview(blurbLabel)

        configure(field: emailField, placeholder: "email@address")
        emailField.keyboardType = .emailAddress
        emailField.autocapitalizationType = .none
        emailField.autocorrectionType = .no
        emailField.textContentType = .username

        configure(field: passwordField, placeholder: "password (6+ characters)")
        passwordField.isSecureTextEntry = true
        passwordField.textContentType = .password

        configure(field: nameField, placeholder: "hero name")
        nameField.isHidden = true

        configure(field: serverField, placeholder: "server url")
        serverField.text = GameService.shared.serverURL.absoluteString
        serverField.keyboardType = .URL
        serverField.autocapitalizationType = .none
        serverField.autocorrectionType = .no
        serverField.returnKeyType = .done

        emailField.delegate = self
        passwordField.delegate = self
        nameField.delegate = self
        serverField.delegate = self

        style(button: primaryButton, color: Theme.goldBright)
        primaryButton.setTitle("SIGN IN", for: .normal)
        primaryButton.addTarget(self, action: #selector(primaryTapped), for: .touchUpInside)

        style(button: switchButton, color: Theme.stone)
        switchButton.setTitleColor(Theme.textPrimary, for: .normal)
        switchButton.setTitle("NEW HERE?  CREATE AN ACCOUNT", for: .normal)
        switchButton.addTarget(self, action: #selector(switchTapped), for: .touchUpInside)

        style(button: offlineButton, color: Theme.panel)
        offlineButton.setTitleColor(Theme.textSecondary, for: .normal)
        offlineButton.setTitle("PLAY OFFLINE", for: .normal)
        offlineButton.addTarget(self, action: #selector(offlineTapped), for: .touchUpInside)

        statusLabel.font = Theme.body(14)
        statusLabel.textColor = Theme.danger
        statusLabel.textAlignment = .center
        statusLabel.numberOfLines = 0
        addSubview(statusLabel)

        let controls: [UIView] = [emailField, passwordField, nameField, serverField,
                                  primaryButton, switchButton, offlineButton]
        for control in controls {
            control.autoresizingMask = []
            addSubview(control)
        }

        let tap = UITapGestureRecognizer(target: self, action: #selector(dismissKeyboard))
        tap.cancelsTouchesInView = false
        addGestureRecognizer(tap)
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = min(620, bounds.width - 60)
        let x = (bounds.width - width) / 2
        var y = bounds.height * 0.10
        titleLabel.frame = CGRect(x: x, y: y, width: width, height: 44)
        y += 46
        blurbLabel.frame = CGRect(x: x, y: y, width: width, height: 44)
        y += 64

        let fieldH: CGFloat = 46
        let gap: CGFloat = 12
        emailField.frame = CGRect(x: x, y: y, width: width, height: fieldH)
        y += fieldH + gap
        passwordField.frame = CGRect(x: x, y: y, width: width, height: fieldH)
        y += fieldH + gap
        nameField.frame = CGRect(x: x, y: y, width: width, height: fieldH)
        if nameField.isHidden { y += gap } else { y += fieldH + gap }
        primaryButton.frame = CGRect(x: x, y: y, width: width, height: 50)
        y += 50 + gap
        switchButton.frame = CGRect(x: x, y: y, width: width, height: 40)
        y += 40 + gap
        offlineButton.frame = CGRect(x: x, y: y, width: width, height: 44)
        y += 44 + 8
        statusLabel.frame = CGRect(x: x, y: y, width: width, height: 36)

        let serverY = bounds.height - 52
        serverField.frame = CGRect(x: x, y: serverY, width: width, height: 40)
    }

    // MARK: Styling

    private func configure(field: UITextField, placeholder: String) {
        field.placeholder = placeholder
        field.backgroundColor = UIColor.black.withAlphaComponent(0.55)
        field.layer.borderColor = Theme.stone.cgColor
        field.layer.borderWidth = 1
        field.layer.cornerRadius = 6
        field.textColor = Theme.textPrimary
        field.font = Theme.body(16)
        let pad = UIView(frame: CGRect(x: 0, y: 0, width: 12, height: 12))
        field.leftView = pad
        field.leftViewMode = .always
    }

    private func style(button: UIButton, color: UIColor) {
        button.titleLabel?.font = Theme.display(16)
        button.setTitleColor(Theme.void, for: .normal)
        button.backgroundColor = color
        Theme.decorate(button, cornerRadius: 6)
    }

    // MARK: Actions

    @objc private func primaryTapped() {
        guard !busy else { return }
        let email = (emailField.text ?? "").trimmingCharacters(in: .whitespaces)
        let password = passwordField.text ?? ""
        let name = (nameField.text ?? "").trimmingCharacters(in: .whitespaces)
        guard email.contains("@"), email.count >= 5 else {
            statusLabel.text = "Enter a valid email address."
            return
        }
        guard password.count >= 6 else {
            statusLabel.text = "Password needs at least 6 characters."
            return
        }
        if registerMode && name.count < 2 {
            statusLabel.text = "Give your hero a name."
            return
        }
        GameService.shared.setServerURL(serverField.text ?? "")
        endEditing(true)

        busy = true
        primaryButton.isEnabled = false
        statusLabel.textColor = Theme.textSecondary
        statusLabel.text = registerMode ? "Creating your account…" : "Signing in…"

        Task { [weak self] in
            guard let self = self else { return }
            do {
                if self.registerMode {
                    _ = try await GameService.shared.register(email: email,
                                                              password: password,
                                                              name: name)
                } else {
                    _ = try await GameService.shared.login(email: email,
                                                           password: password)
                }
                self.onDone?(true)
            } catch {
                self.busy = false
                self.primaryButton.isEnabled = true
                self.statusLabel.textColor = Theme.danger
                self.statusLabel.text = error.playerMessage
            }
        }
    }

    @objc private func switchTapped() {
        registerMode.toggle()
        nameField.isHidden = !registerMode
        primaryButton.setTitle(registerMode ? "CREATE ACCOUNT" : "SIGN IN", for: .normal)
        switchButton.setTitle(registerMode ? "HAVE AN ACCOUNT?  SIGN IN"
                                           : "NEW HERE?  CREATE AN ACCOUNT",
                              for: .normal)
        statusLabel.text = ""
        setNeedsLayout()
    }

    @objc private func offlineTapped() {
        GameService.shared.setServerURL(serverField.text ?? "")
        endEditing(true)
        onDone?(false)
    }

    @objc private func dismissKeyboard() {
        endEditing(true)
    }

    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        if textField === emailField {
            passwordField.becomeFirstResponder()
        } else if textField === passwordField && registerMode {
            nameField.becomeFirstResponder()
        } else {
            textField.resignFirstResponder()
        }
        return true
    }
}
