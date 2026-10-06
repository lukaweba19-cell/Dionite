// ============================================================================
// Dionite — HUD.
//
// A UIKit overlay drawn on top of the Metal view. Every value comes from the
// C++ runtime through DioniteBridge; nothing here owns game state.
//
// Visual language follows design_guidelines.json: void-black panels, stone and
// gold borders, serif display type, monospaced numerals, and the Diablo IV
// orb + hotbar arrangement adapted for touch.
// ============================================================================
import UIKit
import simd

// Swift-side mirrors of the C enums so the HUD never depends on how Clang
// imports a plain `typedef enum`.
private enum HudText: Int32 {
    case className = 0
    case resourceName
    case region
    case questTitle
    case questObjective
    case boss
    case prompt
}

private enum EventKind: Int32 {
    case none = 0
    case info
    case loot
    case levelUp
    case quest
    case boss
    case danger
}

// MARK: - Theme

enum Theme {
    static let void = UIColor(red: 0.020, green: 0.020, blue: 0.020, alpha: 1)
    static let panel = UIColor(red: 0.063, green: 0.063, blue: 0.078, alpha: 0.94)
    static let stone = UIColor(red: 0.227, green: 0.212, blue: 0.196, alpha: 1)
    static let gold = UIColor(red: 0.647, green: 0.522, blue: 0.298, alpha: 1)
    static let goldBright = UIColor(red: 0.878, green: 0.729, blue: 0.427, alpha: 1)
    static let textPrimary = UIColor(red: 0.894, green: 0.863, blue: 0.827, alpha: 1)
    static let textSecondary = UIColor(red: 0.639, green: 0.620, blue: 0.588, alpha: 1)
    static let textMuted = UIColor(red: 0.431, green: 0.408, blue: 0.384, alpha: 1)
    static let health = UIColor(red: 0.863, green: 0.149, blue: 0.149, alpha: 1)
    static let healthDark = UIColor(red: 0.361, green: 0.055, blue: 0.055, alpha: 1)
    static let resource = UIColor(red: 0.145, green: 0.388, blue: 0.922, alpha: 1)
    static let resourceDark = UIColor(red: 0.055, green: 0.145, blue: 0.361, alpha: 1)
    static let xp = UIColor(red: 0.984, green: 0.749, blue: 0.141, alpha: 1)
    static let danger = UIColor(red: 0.863, green: 0.200, blue: 0.278, alpha: 1)

    static func rarity(_ value: Int32) -> UIColor {
        switch value {
        case 1: return UIColor(red: 0.231, green: 0.510, blue: 0.965, alpha: 1) // magic
        case 2: return UIColor(red: 0.984, green: 0.749, blue: 0.212, alpha: 1) // rare
        case 3: return UIColor(red: 0.659, green: 0.333, blue: 0.969, alpha: 1) // epic
        case 4: return UIColor(red: 0.976, green: 0.451, blue: 0.086, alpha: 1) // legendary
        case 5: return UIColor(red: 0.882, green: 0.114, blue: 0.282, alpha: 1) // mythic
        default: return UIColor(red: 0.690, green: 0.690, blue: 0.690, alpha: 1)
        }
    }

    static func display(_ size: CGFloat) -> UIFont {
        return UIFont(name: "Georgia-Bold", size: size) ?? UIFont.boldSystemFont(ofSize: size)
    }

    static func body(_ size: CGFloat) -> UIFont {
        return UIFont(name: "Georgia", size: size) ?? UIFont.systemFont(ofSize: size)
    }

    static func mono(_ size: CGFloat) -> UIFont {
        return UIFont.monospacedSystemFont(ofSize: size, weight: .semibold)
    }

    /// Shared ornate panel treatment: stone hairline over a gold inner edge.
    static func decorate(_ view: UIView, cornerRadius: CGFloat = 6) {
        view.layer.cornerRadius = cornerRadius
        view.layer.borderWidth = 1
        view.layer.borderColor = stone.cgColor
        view.layer.masksToBounds = true
    }
}

// MARK: - Orb

final class OrbView: UIView {
    var fraction: Float = 1 { didSet { setNeedsLayout() } }
    var liquidColor: UIColor = Theme.health
    var trackColor: UIColor = Theme.healthDark

    private let well = UIView()
    private let fill = UIView()
    private let gloss = UIView()

    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        well.backgroundColor = trackColor
        well.clipsToBounds = true
        addSubview(well)
        addSubview(fill)
        gloss.backgroundColor = UIColor(white: 1, alpha: 0.10)
        addSubview(gloss)
        layer.borderColor = Theme.gold.cgColor
        layer.borderWidth = 2
        layer.cornerRadius = 8
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func layoutSubviews() {
        super.layoutSubviews()
        well.frame = bounds.insetBy(dx: 3, dy: 3)
        well.layer.cornerRadius = well.bounds.width / 2
        fill.backgroundColor = liquidColor
        let visible = well.bounds.height * CGFloat(max(0, min(1, fraction)))
        fill.frame = CGRect(x: 0, y: well.bounds.height - visible,
                            width: well.bounds.width, height: visible)
        fill.layer.cornerRadius = 2
        gloss.frame = CGRect(x: 3, y: 3, width: bounds.width - 6, height: bounds.height * 0.42)
        gloss.layer.cornerRadius = gloss.bounds.width / 2
        layer.cornerRadius = bounds.width / 2
    }
}

// MARK: - Ability slot

final class AbilitySlotView: UIView {
    var onTap: ((Int) -> Void)?
    private(set) var index = 0

    private let surface = UIView()
    private let nameLabel = UILabel()
    private let cooldownLabel = UILabel()
    private let wipe = UIView()
    private let readyRing = UIView()

    func configure(index: Int) {
        self.index = index        surface.backgroundColor = Theme.panel
        Theme.decorate(surface, cornerRadius: 8)
        addSubview(surface)

        wipe.backgroundColor = UIColor(white: 0, alpha: 0.68)
        wipe.layer.cornerRadius = 8
        wipe.clipsToBounds = true
        surface.addSubview(wipe)

        nameLabel.numberOfLines = 2
        nameLabel.textAlignment = .center
        nameLabel.font = Theme.body(10)
        nameLabel.textColor = Theme.textPrimary
        nameLabel.adjustsFontSizeToFitWidth = true
        nameLabel.minimumScaleFactor = 0.6
        surface.addSubview(nameLabel)

        cooldownLabel.textAlignment = .center
        cooldownLabel.font = Theme.mono(13)
        cooldownLabel.textColor = Theme.goldBright
        surface.addSubview(cooldownLabel)

        readyRing.layer.cornerRadius = 8
        readyRing.layer.borderWidth = 1.5
        readyRing.layer.borderColor = Theme.gold.cgColor
        readyRing.isUserInteractionEnabled = false
        addSubview(readyRing)

        let tap = UITapGestureRecognizer(target: self, action: #selector(tapped))
        addGestureRecognizer(tap)
    }

    @objc private func tapped() { onTap?(index) }

    override func layoutSubviews() {
        super.layoutSubviews()
        surface.frame = bounds
        readyRing.frame = bounds.insetBy(dx: -1.5, dy: -1.5)
        nameLabel.frame = surface.bounds.insetBy(dx: 3, dy: 10)
        cooldownLabel.frame = CGRect(x: 0, y: bounds.midY - 9, width: bounds.width, height: 18)
        let fraction = max(0, min(1, wipeFraction))
        wipe.frame = CGRect(x: 0, y: 0, width: bounds.width, height: bounds.height * fraction)
    }

    private var wipeFraction: Float = 0

    override init(frame: CGRect) { super.init(frame: frame) }

    func update(ability: AbilityState) {
        nameLabel.text = ability.name.uppercased()
        let fraction: Float
        if ability.cooldown > 0 && ability.cooldownMax > 0.01 {
            fraction = ability.cooldown / ability.cooldownMax
        } else {
            fraction = 0
        }
        wipeFraction = fraction
        cooldownLabel.text = ability.cooldown > 0.05
            ? String(format: "%.0f", ceil(Double(ability.cooldown)))
            : ""
        readyRing.layer.borderColor = ability.ready ? Theme.goldBright.cgColor
                                                   : Theme.stone.cgColor
        readyRing.alpha = ability.ready ? 1 : 0.45
        setNeedsLayout()
    }
}

// MARK: - Toast

private final class ToastView: UIView {
    private let label = UILabel()

    init(text: String, tint: UIColor) {
        super.init(frame: .zero)
        backgroundColor = Theme.panel.withAlphaComponent(0.92)
        Theme.decorate(self, cornerRadius: 4)
        layer.borderColor = tint.cgColor

        let stripe = UIView()
        stripe.backgroundColor = tint
        stripe.frame = CGRect(x: 0, y: 0, width: 3, height: 60)
        stripe.autoresizingMask = [.flexibleHeight]
        addSubview(stripe)

        label.text = text
        label.font = Theme.body(14)
        label.textColor = Theme.textPrimary
        label.textAlignment = .center
        label.numberOfLines = 2
        label.frame = bounds.insetBy(dx: 12, dy: 4)
        label.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        addSubview(label)
        alpha = 0
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override var intrinsicContentSize: CGSize { return CGSize(width: 460, height: 34) }
}

// MARK: - HUD

final class HUDView: UIView {
    // Input and navigation callbacks — the view owns no game state.
    var onAbility: ((Int, Bool) -> Void)?
    var onFire: ((Bool) -> Void)?
    var onDash: ((Bool) -> Void)?
    var onPotion: (() -> Void)?
    var onInteract: (() -> Void)?
    var onRespawn: (() -> Void)?
    var onMenu: (() -> Void)?

    private let bridge = DioniteBridge.shared

    private let levelBadge = UIView()
    private let levelLabel = UILabel()
    private let classLabel = UILabel()
    private let goldLabel = UILabel()
    private let xpTrack = UIView()
    private let xpFill = UIView()

    private let bossCard = UIView()
    private let bossName = UILabel()
    private let bossTrack = UIView()
    private let bossFill = UIView()

    private let promptPill = UILabel()
    private let promptButton = UIButton(type: .system)
    private let toastColumn = UIStackView()

    private let questCard = UIView()
    private let questTitle = UILabel()
    private let questObjective = UILabel()
    private let menuButton = UIButton(type: .system)

    private let healthOrb = OrbView()
    private let resourceOrb = OrbView()
    private let potionBadge = UILabel()
    private let slots: [AbilitySlotView] =
        (0..<DI_MAX_ABILITIES).map { _ in AbilitySlotView(frame: .zero) }

    private let fireButton = UIButton(type: .system)
    private let dashButton = UIButton(type: .system)

    private let deathOverlay = UIView()
    private let deathTitle = UILabel()
    private let deathSubtitle = UILabel()

    private let damageLabels: [UILabel] = (0..<12).map { _ in
        let label = UILabel()
        label.font = Theme.mono(15)
        label.textAlignment = .center
        label.layer.shadowColor = UIColor.black.cgColor
        label.layer.shadowOpacity = 0.9
        label.layer.shadowRadius = 2
        label.layer.shadowOffset = CGSize(width: 0, height: 1)
        label.isHidden = true
        return label
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = true
        backgroundColor = .clear
        buildStaticUI()
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func buildStaticUI() {
        levelBadge.backgroundColor = Theme.panel
        levelBadge.layer.borderColor = Theme.gold.cgColor
        levelBadge.layer.borderWidth = 2
        levelBadge.layer.cornerRadius = 28
        addSubview(levelBadge)

        levelLabel.font = Theme.display(24)
        levelLabel.textColor = Theme.goldBright
        levelLabel.textAlignment = .center
        levelBadge.addSubview(levelLabel)

        classLabel.font = Theme.display(17)
        classLabel.textColor = Theme.textPrimary
        addSubview(classLabel)

        goldLabel.font = Theme.mono(13)
        goldLabel.textColor = Theme.xp
        addSubview(goldLabel)

        xpTrack.backgroundColor = UIColor(white: 1, alpha: 0.10)
        Theme.decorate(xpTrack, cornerRadius: 2)
        addSubview(xpTrack)
        xpFill.backgroundColor = Theme.xp
        xpFill.layer.cornerRadius = 2
        xpTrack.addSubview(xpFill)

        bossCard.backgroundColor = Theme.panel
        Theme.decorate(bossCard, cornerRadius: 5)
        bossCard.isHidden = true
        addSubview(bossCard)
        bossName.font = Theme.display(14)
        bossName.textColor = Theme.textPrimary
        bossName.textAlignment = .center
        bossCard.addSubview(bossName)
        bossTrack.backgroundColor = UIColor(white: 0, alpha: 0.65)
        Theme.decorate(bossTrack, cornerRadius: 3)
        bossCard.addSubview(bossTrack)
        bossFill.backgroundColor = Theme.danger
        bossFill.layer.cornerRadius = 3
        bossTrack.addSubview(bossFill)

        promptPill.font = Theme.body(15)
        promptPill.textColor = Theme.goldBright
        promptPill.textAlignment = .center
        promptPill.backgroundColor = Theme.panel.withAlphaComponent(0.9)
        Theme.decorate(promptPill, cornerRadius: 13)
        promptPill.isHidden = true
        addSubview(promptPill)

        promptButton.titleLabel?.font = Theme.display(13)
        promptButton.setTitleColor(Theme.void, for: .normal)
        promptButton.backgroundColor = Theme.goldBright
        Theme.decorate(promptButton, cornerRadius: 13)
        promptButton.isHidden = true
        promptButton.addTarget(self, action: #selector(interactTapped), for: .touchUpInside)
        addSubview(promptButton)

        toastColumn.axis = .vertical
        toastColumn.spacing = 6
        toastColumn.alignment = .fill
        addSubview(toastColumn)

        questCard.backgroundColor = Theme.panel
        Theme.decorate(questCard, cornerRadius: 5)
        addSubview(questCard)
        questTitle.font = Theme.display(13)
        questTitle.textColor = Theme.goldBright
        questObjective.font = Theme.mono(12)
        questObjective.textColor = Theme.textSecondary
        questObjective.adjustsFontSizeToFitWidth = true
        questObjective.minimumScaleFactor = 0.7
        questCard.addSubview(questTitle)
        questCard.addSubview(questObjective)

        configureCircle(menuButton, diameter: 46, title: "☰")
        menuButton.addTarget(self, action: #selector(menuTapped), for: .touchUpInside)
        addSubview(menuButton)

        configureOrb(healthOrb, liquid: Theme.health, track: Theme.healthDark)
        healthOrb.addGestureRecognizer(UITapGestureRecognizer(target: self,
                                                              action: #selector(potionTapped)))
        healthOrb.isUserInteractionEnabled = true
        addSubview(healthOrb)

        configureOrb(resourceOrb, liquid: Theme.resource, track: Theme.resourceDark)
        addSubview(resourceOrb)

        potionBadge.font = Theme.mono(12)
        potionBadge.textColor = Theme.textPrimary
        potionBadge.textAlignment = .center
        potionBadge.backgroundColor = Theme.panel
        potionBadge.layer.borderColor = Theme.gold.cgColor
        potionBadge.layer.borderWidth = 1
        potionBadge.layer.cornerRadius = 9
        addSubview(potionBadge)

        for (index, slot) in slots.enumerated() {
            slot.configure(index: index)
            slot.onTap = { [weak self] tapped in self?.onAbility?(tapped, true) }
            addSubview(slot)
        }

        configureCircle(fireButton, diameter: 78, title: "FIRE")
        fireButton.titleLabel?.font = Theme.display(14)
        fireButton.backgroundColor = Theme.danger
        fireButton.layer.borderColor = Theme.gold.cgColor
        fireButton.addTarget(self, action: #selector(fireDown), for: .touchDown)
        fireButton.addTarget(self, action: #selector(fireUp),
                             for: [.touchUpInside, .touchUpOutside, .touchCancel])
        addSubview(fireButton)

        configureCircle(dashButton, diameter: 64, title: "DASH")
        dashButton.titleLabel?.font = Theme.display(11)
        dashButton.backgroundColor = Theme.panel
        dashButton.addTarget(self, action: #selector(dashDown), for: .touchDown)
        dashButton.addTarget(self, action: #selector(dashUp),
                             for: [.touchUpInside, .touchUpOutside, .touchCancel])
        addSubview(dashButton)

        deathOverlay.backgroundColor = Theme.void.withAlphaComponent(0.86)
        deathOverlay.isHidden = true
        addSubview(deathOverlay)
        deathTitle.font = Theme.display(40)
        deathTitle.textColor = Theme.danger
        deathTitle.textAlignment = .center
        deathTitle.text = "YOU HAVE FALLEN"
        deathOverlay.addSubview(deathTitle)
        deathSubtitle.font = Theme.body(18)
        deathSubtitle.textColor = Theme.textSecondary
        deathSubtitle.textAlignment = .center
        deathSubtitle.text = "Tap anywhere to rise again"
        deathOverlay.addSubview(deathSubtitle)
        let rise = UITapGestureRecognizer(target: self, action: #selector(respawnTapped))
        deathOverlay.addGestureRecognizer(rise)

        for label in damageLabels { addSubview(label) }
    }

    private func configureCircle(_ button: UIButton, diameter: CGFloat, title: String) {
        button.backgroundColor = Theme.panel
        button.layer.borderColor = Theme.gold.cgColor
        button.layer.borderWidth = 1.5
        button.layer.cornerRadius = diameter / 2
        button.setTitle(title, for: .normal)
        button.setTitleColor(Theme.textPrimary, for: .normal)
        button.titleLabel?.font = Theme.display(13)
        button.titleLabel?.textAlignment = .center
    }

    private func configureOrb(_ orb: OrbView, liquid: UIColor, track: UIColor) {
        orb.liquidColor = liquid
        orb.trackColor = track
        orb.backgroundColor = Theme.void
    }

    // MARK: Layout

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = bounds.width
        let height = bounds.height
        let midX = width / 2

        levelBadge.frame = CGRect(x: 14, y: 10, width: 56, height: 56)
        levelLabel.frame = levelBadge.bounds
        classLabel.frame = CGRect(x: 78, y: 12, width: 210, height: 24)
        goldLabel.frame = CGRect(x: 78, y: 38, width: 210, height: 18)
        xpTrack.frame = CGRect(x: 78, y: 60, width: 210, height: 5)
        xpFill.frame = CGRect(x: 0, y: 0, width: xpTrack.bounds.width * xpFraction, height: 5)

        let bossWidth = min(520, width * 0.46)
        bossCard.frame = CGRect(x: midX - bossWidth / 2, y: 8, width: bossWidth, height: 44)
        bossName.frame = CGRect(x: 8, y: 3, width: bossWidth - 16, height: 18)
        bossTrack.frame = CGRect(x: 10, y: 25, width: bossWidth - 20, height: 11)
        bossFill.frame = CGRect(x: 0, y: 0,
                                width: bossTrack.bounds.width * CGFloat(bossFraction),
                                height: 11)

        let pillWidth: CGFloat = 320
        promptPill.frame = CGRect(x: midX - pillWidth / 2, y: 56, width: pillWidth, height: 28)
        promptButton.frame = CGRect(x: midX - 62, y: 88, width: 124, height: 28)

        toastColumn.frame = CGRect(x: midX - 240, y: 122, width: 480, height: 110)

        questCard.frame = CGRect(x: width - 336, y: 10, width: 268, height: 56)
        questTitle.frame = CGRect(x: 10, y: 5, width: 248, height: 18)
        questObjective.frame = CGRect(x: 10, y: 27, width: 248, height: 20)
        menuButton.frame = CGRect(x: width - 58, y: 10, width: 46, height: 46)

        healthOrb.frame = CGRect(x: 12, y: height - 100, width: 86, height: 86)
        resourceOrb.frame = CGRect(x: width - 98, y: height - 100, width: 86, height: 86)
        potionBadge.frame = CGRect(x: 12, y: height - 32, width: 86, height: 18)

        let slotSize: CGFloat = 46
        let gap: CGFloat = 7
        let barWidth = CGFloat(DI_MAX_ABILITIES) * slotSize
            + CGFloat(DI_MAX_ABILITIES - 1) * gap
        var slotX = midX - barWidth / 2
        let slotY = height - 74
        for slot in slots {
            slot.frame = CGRect(x: slotX, y: slotY, width: slotSize, height: slotSize)
            slotX += slotSize + gap
        }

        fireButton.frame = CGRect(x: width - 100, y: height - 204, width: 78, height: 78)
        dashButton.frame = CGRect(x: width - 194, y: height - 190, width: 64, height: 64)

        deathOverlay.frame = bounds
        deathTitle.frame = CGRect(x: 0, y: height / 2 - 70, width: width, height: 52)
        deathSubtitle.frame = CGRect(x: 0, y: height / 2 - 12, width: width, height: 30)
    }

    // MARK: State

    private var xpFraction: Float = 0
    private var bossFraction: Float = 0

    /// Refreshes every widget from the runtime snapshot.
    func update(hud: DIHud) {
        levelLabel.text = "\(hud.level)"
        classLabel.text = bridge.hudText(HudText.className.rawValue)
        goldLabel.text = "\(hud.gold)  ·  \(bridge.hudText(HudText.region.rawValue))"
        xpFraction = hud.xpFrac

        healthOrb.fraction = hud.healthMax > 0 ? hud.health / hud.healthMax : 0
        resourceOrb.fraction = hud.resourceMax > 0 ? hud.resource / hud.resourceMax : 0
        potionBadge.text = "POTIONS  \(hud.potions)"

        bossCard.isHidden = hud.bossActive == 0
        if hud.bossActive != 0 {
            bossName.text = bridge.hudText(HudText.boss.rawValue)
            bossFraction = hud.bossFrac
        }

        let prompt = bridge.hudText(HudText.prompt.rawValue)
        promptPill.text = prompt.isEmpty ? nil : "‹ \(prompt) ›"
        promptPill.isHidden = prompt.isEmpty
        promptButton.setTitle(prompt.uppercased(), for: .normal)
        promptButton.isHidden = prompt.isEmpty

        questTitle.text = bridge.hudText(HudText.questTitle.rawValue)
        questObjective.text = bridge.hudText(HudText.questObjective.rawValue)

        for (index, slot) in slots.enumerated() {
            slot.update(ability: bridge.ability(index))
        }

        deathOverlay.isHidden = hud.dead == 0
        setNeedsLayout()
    }

    private var toastSequence = 0

    func show(event: WorldEvent) {
        let tint: UIColor
        switch event.kind {
        case EventKind.loot.rawValue: tint = Theme.goldBright
        case EventKind.levelUp.rawValue: tint = Theme.xp
        case EventKind.quest.rawValue: tint = UIColor(red: 0.2, green: 0.78, blue: 0.5, alpha: 1)
        case EventKind.boss.rawValue: tint = Theme.danger
        case EventKind.danger.rawValue: tint = Theme.danger
        default: tint = Theme.textSecondary
        }
        let toast = ToastView(text: event.text, tint: tint)
        toastSequence += 1
        let tag = toastSequence
        toast.tag = tag
        toastColumn.addArrangedSubview(toast)
        while toastColumn.arrangedSubviews.count > 3 {
            toastColumn.arrangedSubviews.first?.removeFromSuperview()
        }
        UIView.animate(withDuration: 0.22, animations: { toast.alpha = 1 }) { _ in
            UIView.animate(withDuration: 0.4, delay: 2.6, options: [], animations: {
                toast.alpha = 0
            }, completion: { _ in
                if toast.tag == tag { toast.removeFromSuperview() }
            })
        }
        toast.translatesAutoresizingMaskIntoConstraints = false
        toast.heightAnchor.constraint(equalToConstant: 34).isActive = true
    }

    /// Positions floating damage numbers using the current view-projection.
    func show(damage numbers: [DIDamageNumber], viewProj: simd_float4x4, size: CGSize) {
        for (index, label) in damageLabels.enumerated() {
            guard index < numbers.count else {
                label.isHidden = true
                continue
            }
            let number = numbers[index]
            let clip = viewProj * SIMD4<Float>(number.x, number.y, number.z, 1)
            guard clip.w > 0.001 else {
                label.isHidden = true
                continue
            }
            let ndcX = clip.x / clip.w
            let ndcY = clip.y / clip.w
            guard abs(ndcX) < 1.25, abs(ndcY) < 1.25 else {
                label.isHidden = true
                continue
            }
            let progress = number.maxAge > 0 ? number.age / number.maxAge : 1
            let rise = CGFloat(progress) * 46
            label.isHidden = false
            label.text = number.crit != 0
                ? String(format: "%.0f!", number.value)
                : String(format: "%.0f", number.value)
            label.textColor = number.crit != 0 ? Theme.xp : Theme.textPrimary
            label.font = Theme.mono(number.crit != 0 ? 19 : 15)
            label.alpha = CGFloat(max(0, 1 - progress))
            let originX = (ndcX * 0.5 + 0.5) * size.width
            let originY = (1 - (ndcY * 0.5 + 0.5)) * size.height
            label.frame = CGRect(x: originX - 44, y: originY - 16 - rise,
                                 width: 88, height: 24)
        }
    }

    // MARK: Touch routing

    /// Decorative panels must not swallow world taps, so anything that is not
    /// an actual control falls through to the Metal view below.
    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard let hit = super.hitTest(point, with: event) else { return nil }
        return isInteractive(hit) ? hit : nil
    }

    private func isInteractive(_ start: UIView) -> Bool {
        var current: UIView? = start
        while let candidate = current, candidate !== self {
            if candidate is UIControl { return true }
            if let gestures = candidate.gestureRecognizers, !gestures.isEmpty { return true }
            current = candidate.superview
        }
        return false
    }

    // MARK: Actions

    @objc private func interactTapped() { onInteract?() }
    @objc private func menuTapped() { onMenu?() }
    @objc private func potionTapped() { onPotion?() }
    @objc private func respawnTapped() { onRespawn?() }
    @objc private func fireDown() { onFire?(true) }
    @objc private func fireUp() { onFire?(false) }
    @objc private func dashDown() { onDash?(true) }
    @objc private func dashUp() { onDash?(false) }
}
