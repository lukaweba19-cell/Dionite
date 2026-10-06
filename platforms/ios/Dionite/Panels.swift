// ============================================================================
// Dionite — In-game menu overlay (Pack / Realms / Spire).
//
// Presenting the three "there is more to do here" surfaces in one place keeps
// the touch HUD uncluttered while still exposing inventory management, fast
// travel across unlocked biomes, and the Infinity Spire endgame.
// ============================================================================
import UIKit

final class GameMenuView: UIView {
    var onClose: (() -> Void)?
    var onEquip: ((Int32) -> Void)?
    var onTravel: ((Int32) -> Void)?
    var onStartSpire: (() -> Void)?
    var onExitSpire: (() -> Void)?
    var onSave: (() -> Void)?

    private let dim = UIView()
    private let panel = UIView()
    private let titleLabel = UILabel()
    private let closeButton = UIButton(type: .system)
    private let tabBar = UIStackView()
    private let tabButtons: [UIButton] = (0..<3).map { _ in UIButton(type: .system) }
    private let scrollView = UIScrollView()
    private let content = UIStackView()
    private var activeTab = 0

    private let tabTitles = ["PACK", "REALMS", "SPIRE"]

    override init(frame: CGRect) {
        super.init(frame: frame)
        isHidden = true

        dim.backgroundColor = UIColor.black.withAlphaComponent(0.78)
        dim.frame = bounds
        dim.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        let dismiss = UITapGestureRecognizer(target: self, action: #selector(closeTapped))
        dim.addGestureRecognizer(dismiss)
        addSubview(dim)

        panel.backgroundColor = Theme.panel
        Theme.decorate(panel, cornerRadius: 10)
        panel.layer.borderColor = Theme.gold.cgColor
        addSubview(panel)

        titleLabel.text = "DIONITE"
        titleLabel.font = Theme.display(24)
        titleLabel.textColor = Theme.goldBright
        titleLabel.textAlignment = .center
        panel.addSubview(titleLabel)

        closeButton.setTitle("✕", for: .normal)
        closeButton.titleLabel?.font = Theme.display(20)
        closeButton.setTitleColor(Theme.textSecondary, for: .normal)
        closeButton.addTarget(self, action: #selector(closeTapped), for: .touchUpInside)
        panel.addSubview(closeButton)

        tabBar.axis = .horizontal
        tabBar.distribution = .fillEqually
        tabBar.spacing = 8
        panel.addSubview(tabBar)
        for (index, button) in tabButtons.enumerated() {
            button.setTitle(tabTitles[index], for: .normal)
            button.titleLabel?.font = Theme.display(15)
            button.layer.cornerRadius = 5
            button.layer.borderWidth = 1
            button.addTarget(self, action: #selector(tabTapped(_:)), for: .touchUpInside)
            button.tag = index
            tabBar.addArrangedSubview(button)
        }

        scrollView.showsVerticalScrollIndicator = true
        scrollView.alwaysBounceVertical = true
        panel.addSubview(scrollView)

        content.axis = .vertical
        content.spacing = 8
        content.alignment = .fill
        content.translatesAutoresizingMaskIntoConstraints = false
        scrollView.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: scrollView.contentLayoutGuide.leadingAnchor),
            content.trailingAnchor.constraint(equalTo: scrollView.contentLayoutGuide.trailingAnchor),
            content.topAnchor.constraint(equalTo: scrollView.contentLayoutGuide.topAnchor),
            content.bottomAnchor.constraint(equalTo: scrollView.contentLayoutGuide.bottomAnchor),
            content.widthAnchor.constraint(equalTo: scrollView.frameLayoutGuide.widthAnchor)
        ])
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func layoutSubviews() {
        super.layoutSubviews()
        let width = min(720, bounds.width - 48)
        let height = min(bounds.height - 60, 520)
        panel.frame = CGRect(x: (bounds.width - width) / 2,
                             y: (bounds.height - height) / 2,
                             width: width, height: height)
        titleLabel.frame = CGRect(x: 60, y: 14, width: width - 120, height: 32)
        closeButton.frame = CGRect(x: width - 52, y: 12, width: 40, height: 36)
        tabBar.frame = CGRect(x: 16, y: 56, width: width - 32, height: 38)
        scrollView.frame = CGRect(x: 16, y: 104, width: width - 32, height: height - 120)
    }

    // MARK: Data

    func reload(hud: DIHud, spireBest: Int32) {
        highlightTab()
        content.arrangedSubviews.forEach { $0.removeFromSuperview() }

        switch activeTab {
        case 1: buildRealms(hud: hud)
        case 2: buildSpire(hud: hud, best: spireBest)
        default: buildPack()
        }
    }

    private func buildPack() {
        let count = DioniteBridge.shared.inventoryCount
        if count == 0 {
            content.addArrangedSubview(caption("Your pack is empty. Slay, loot, return."))
            return
        }
        for index in 0..<count {
            guard let row = DioniteBridge.shared.inventoryRow(index) else { continue }
            let item = ItemRowView(row: row)
            item.onTap = { [weak self] in self?.onEquip?(index) }
            content.addArrangedSubview(item)
        }
    }

    private func buildRealms(hud: DIHud) {
        let names = ["Verdant Wilds", "Ashen Wastes", "Sunken Crypts",
                     "Frozen Spire", "Sky Citadel"]
        for (index, name) in names.enumerated() {
            let unlocked = Int32(index) < hud.unlockedRegions
            let row = RealmRowView(name: name,
                                   index: index,
                                   current: index == Int(hud.region),
                                   unlocked: unlocked)
            if unlocked {
                row.onTap = { [weak self] in self?.onTravel?(Int32(index)) }
            }
            content.addArrangedSubview(row)
        }
        let save = UIButton(type: .system)
        save.setTitle("SAVE PROGRESS", for: .normal)
        save.titleLabel?.font = Theme.display(15)
        save.setTitleColor(Theme.void, for: .normal)
        save.backgroundColor = Theme.goldBright
        Theme.decorate(save, cornerRadius: 6)
        save.heightAnchor.constraint(equalToConstant: 44).isActive = true
        save.addTarget(self, action: #selector(saveTapped), for: .touchUpInside)
        content.addArrangedSubview(save)
    }

    private func buildSpire(hud: DIHud, best: Int32) {
        let blurb = caption("The Infinity Spire climbs without end. Each floor stacks "
                            + "sigil affixes, and your score is only limited by nerve.")
        content.addArrangedSubview(blurb)
        content.addArrangedSubview(caption("Best floor reached: \(best)"))

        let button = UIButton(type: .system)
        let inside = hud.spireFloor != 0
        button.setTitle(inside ? "LEAVE THE SPIRE" : "ENTER THE SPIRE", for: .normal)
        button.titleLabel?.font = Theme.display(17)
        button.setTitleColor(Theme.void, for: .normal)
        button.backgroundColor = inside ? Theme.danger : Theme.goldBright
        Theme.decorate(button, cornerRadius: 6)
        button.heightAnchor.constraint(equalToConstant: 52).isActive = true
        button.addTarget(self, action: #selector(spireTapped), for: .touchUpInside)
        content.addArrangedSubview(button)
    }

    private func caption(_ text: String) -> UILabel {
        let label = UILabel()
        label.text = text
        label.font = Theme.body(15)
        label.textColor = Theme.textSecondary
        label.numberOfLines = 0
        return label
    }

    // MARK: Actions

    private func highlightTab() {
        for (index, button) in tabButtons.enumerated() {
            let active = index == activeTab
            button.backgroundColor = active ? Theme.gold.withAlphaComponent(0.22) : .clear
            button.layer.borderColor = active ? Theme.gold.cgColor : Theme.stone.cgColor
            button.setTitleColor(active ? Theme.goldBright : Theme.textSecondary, for: .normal)
        }
    }

    @objc private func tabTapped(_ sender: UIButton) {
        activeTab = sender.tag
        reload(hud: currentHud, spireBest: currentSpireBest)
    }

    @objc private func closeTapped() { onClose?() }
    @objc private func saveTapped() { onSave?() }
    @objc private func spireTapped() {
        if currentHud.spireFloor != 0 { onExitSpire?() } else { onStartSpire?() }
    }

    private var currentHud = DIHud()
    private var currentSpireBest: Int32 = 0

    func present(hud: DIHud, spireBest: Int32) {
        currentHud = hud
        currentSpireBest = spireBest
        reload(hud: hud, spireBest: spireBest)
        isHidden = false
        setNeedsLayout()
    }
}

// MARK: - Rows

private final class ItemRowView: UIView {
    var onTap: (() -> Void)?

    init(row: ItemRow) {
        super.init(frame: .zero)
        backgroundColor = Theme.void.withAlphaComponent(0.6)
        Theme.decorate(self, cornerRadius: 5)
        layer.borderColor = Theme.rarity(row.rarity).cgColor
        layer.borderWidth = 1

        let nameLabel = UILabel()
        nameLabel.text = (row.equipped ? "▸ " : "") + row.name
        nameLabel.font = Theme.display(15)
        nameLabel.textColor = Theme.rarity(row.rarity)
        nameLabel.frame = CGRect(x: 12, y: 6, width: bounds.width - 24, height: 20)
        nameLabel.autoresizingMask = [.flexibleWidth]
        addSubview(nameLabel)

        let detailLabel = UILabel()
        let stats = row.detail.isEmpty
            ? "Item level \(row.ilvl)  ·  \(String(format: "%.1f", row.damage)) damage"
            : "\(row.detail)  ·  ilvl \(row.ilvl)"
        detailLabel.text = stats
        detailLabel.font = Theme.mono(12)
        detailLabel.textColor = Theme.textSecondary
        detailLabel.adjustsFontSizeToFitWidth = true
        detailLabel.minimumScaleFactor = 0.7
        detailLabel.frame = CGRect(x: 12, y: 27, width: bounds.width - 24, height: 17)
        detailLabel.autoresizingMask = [.flexibleWidth]
        addSubview(detailLabel)

        heightAnchor.constraint(equalToConstant: 52).isActive = true
        addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(tapped)))
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func tapped() { onTap?() }
}

private final class RealmRowView: UIView {
    var onTap: (() -> Void)?

    init(name: String, index: Int, current: Bool, unlocked: Bool) {
        super.init(frame: .zero)
        backgroundColor = current ? Theme.gold.withAlphaComponent(0.18)
                                  : Theme.void.withAlphaComponent(0.6)
        Theme.decorate(self, cornerRadius: 5)
        layer.borderColor = current ? Theme.gold.cgColor : Theme.stone.cgColor

        let label = UILabel()
        label.text = unlocked ? name : "\(name)  — sealed"
        label.font = Theme.display(16)
        label.textColor = unlocked ? (current ? Theme.goldBright : Theme.textPrimary)
                                   : Theme.textMuted
        label.frame = CGRect(x: 14, y: 8, width: bounds.width - 28, height: 24)
        label.autoresizingMask = [.flexibleWidth]
        addSubview(label)

        heightAnchor.constraint(equalToConstant: 42).isActive = true
        if unlocked {
            addGestureRecognizer(UITapGestureRecognizer(target: self,
                                                        action: #selector(tapped)))
        }
    }

    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    @objc private func tapped() { onTap?() }
}
