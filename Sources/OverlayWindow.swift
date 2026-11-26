import Foundation
import AppKit

final class CatCanvasView: NSView {
    let physics = CatPhysics()
    var onRequestPermission: (() -> Void)?
    var onRightClickMenu: ((NSEvent) -> Void)?
    /// Cat Link: this view shows a friend's cat (no permission pill; petting is also sent to them).
    var isRemote = false
    var onPetStroke: ((_ canvasPoint: CGPoint, _ delta: CGFloat, _ isHead: Bool) -> Void)?
    /// Small name tag above the cat, e.g. "小明 · 在线"; dimmed while the friend is offline.
    var nameLabel: String? { didSet { if nameLabel != oldValue { needsDisplay = true } } }
    var nameLabelDimmed = false
    var positionKey = "KuroCat.Pos"

    private var lastHoverCanvasPt: CGPoint?
    private var isDragging = false
    private var dragStartScreenPt: CGPoint = .zero
    private var dragStartWindowOrigin: CGPoint = .zero
    private var isShowingOpenHand = false

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }
    override var acceptsFirstResponder: Bool { false }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.clear(bounds)

        let scaleX = bounds.width / CatPhysics.canvasSize.width
        let scaleY = bounds.height / CatPhysics.canvasSize.height
        ctx.saveGState()
        ctx.scaleBy(x: scaleX, y: scaleY)

        let showPill = !isRemote && InputEngine.shared.status == .needsPermission
        CatRenderer.draw(in: ctx, physics: physics, showPermissionPill: showPill)
        if let label = nameLabel { drawNameLabel(label, in: ctx) }

        ctx.restoreGState()
    }

    private func drawNameLabel(_ label: String, in ctx: CGContext) {
        let attrs: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11, weight: .semibold),
            .foregroundColor: NSColor.white.withAlphaComponent(nameLabelDimmed ? 0.7 : 1),
        ]
        let text = NSAttributedString(string: label, attributes: attrs)
        let size = text.size()
        let pill = CGRect(x: CatPhysics.seatAnchor.x - size.width / 2 - 9, y: 22, width: size.width + 18, height: size.height + 6)
        ctx.setFillColor(NSColor.black.withAlphaComponent(nameLabelDimmed ? 0.35 : 0.62).cgColor)
        ctx.addPath(CGPath(roundedRect: pill, cornerWidth: pill.height / 2, cornerHeight: pill.height / 2, transform: nil))
        ctx.fillPath()
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        text.draw(at: CGPoint(x: pill.minX + 9, y: pill.minY + 3))
        NSGraphicsContext.restoreGraphicsState()
    }

    func canvasPoint(fromScreenPoint screenPt: CGPoint) -> CGPoint? {
        guard let win = window else { return nil }
        let frame = win.frame
        guard frame.contains(screenPt) else { return nil }
        let localX = (screenPt.x - frame.minX) / frame.width * CatPhysics.canvasSize.width
        // Convert bottom-up screen Y to flipped top-down canvas Y
        let localY = (frame.maxY - screenPt.y) / frame.height * CatPhysics.canvasSize.height
        return CGPoint(x: localX, y: localY)
    }

    /// Called on every 60fps tick to handle smart click-through AND petting strokes.
    func updateHoverAndClickThrough() {
        guard let win = window else { return }
        let screenPt = NSEvent.mouseLocation
        let showPill = !isRemote && InputEngine.shared.status == .needsPermission

        if isDragging {
            win.ignoresMouseEvents = false
            return
        }

        guard let pt = canvasPoint(fromScreenPoint: screenPt) else {
            lastHoverCanvasPt = nil
            if !win.ignoresMouseEvents {
                win.ignoresMouseEvents = true
            }
            resetCursorIfNeeded()
            return
        }

        let zone = CatRenderer.hitTest(point: pt, physics: physics, showPermissionPill: showPill)

        // Petting works in both Smart Mode and Full Click-Through Mode!
        if zone == .head || zone == .body {
            if let prev = lastHoverCanvasPt {
                let delta = hypot(pt.x - prev.x, pt.y - prev.y)
                // Low minimum so slow, gentle strokes still count.
                if delta > 0.5 && delta < 80 {
                    physics.registerPetStroke(canvasPoint: pt, delta: delta, isHead: zone == .head)
                    onPetStroke?(pt, delta, zone == .head)
                }
            }
            lastHoverCanvasPt = pt
        } else {
            lastHoverCanvasPt = nil
        }

        // Smart Click-Through logic (hold Option ⌥ to temporarily unlock Full Click-Through!)
        let optionHeld = NSEvent.modifierFlags.contains(.option)
        if CatSettings.shared.fullClickThrough && !optionHeld {
            // Keep permission pill clickable only if permission isn't granted yet
            win.ignoresMouseEvents = (zone != .permissionPill)
            resetCursorIfNeeded()
        } else {
            let shouldCapture = (zone != .none)
            if win.ignoresMouseEvents == shouldCapture {
                win.ignoresMouseEvents = !shouldCapture
            }
            if zone == .head || zone == .body || zone == .keyboard {
                if !isShowingOpenHand {
                    NSCursor.openHand.set()
                    isShowingOpenHand = true
                }
            } else if zone == .permissionPill {
                NSCursor.pointingHand.set()
                isShowingOpenHand = false
            } else {
                resetCursorIfNeeded()
            }
        }
    }

    private func resetCursorIfNeeded() {
        if isShowingOpenHand {
            NSCursor.arrow.set()
            isShowingOpenHand = false
        }
    }

    override func mouseDown(with event: NSEvent) {
        guard let win = window else { return }
        let screenPt = NSEvent.mouseLocation
        let showPill = !isRemote && InputEngine.shared.status == .needsPermission

        if let pt = canvasPoint(fromScreenPoint: screenPt) {
            let zone = CatRenderer.hitTest(point: pt, physics: physics, showPermissionPill: showPill)
            if zone == .permissionPill {
                onRequestPermission?()
                return
            }
        }

        isDragging = true
        dragStartScreenPt = screenPt
        dragStartWindowOrigin = win.frame.origin
        NSCursor.closedHand.set()
    }

    override func mouseDragged(with event: NSEvent) {
        guard isDragging, let win = window else { return }
        let currentScreenPt = NSEvent.mouseLocation
        let newOrigin = CGPoint(
            x: dragStartWindowOrigin.x + (currentScreenPt.x - dragStartScreenPt.x),
            y: dragStartWindowOrigin.y + (currentScreenPt.y - dragStartScreenPt.y)
        )
        win.setFrameOrigin(newOrigin)
    }

    override func mouseUp(with event: NSEvent) {
        guard isDragging, let win = window else { return }
        isDragging = false
        NSCursor.openHand.set()
        // Save position
        UserDefaults.standard.set(Double(win.frame.origin.x), forKey: positionKey + "X")
        UserDefaults.standard.set(Double(win.frame.origin.y), forKey: positionKey + "Y")
    }

    override func rightMouseDown(with event: NSEvent) {
        onRightClickMenu?(event)
    }
}

final class OverlayWindow: NSPanel {
    let catView = CatCanvasView()
    private let positionKey: String
    private let defaultShiftX: CGFloat

    /// `positionKey` keeps each cat's saved position separate; `defaultShiftX` moves the default
    /// spot (a friend's cat sits to the left of yours).
    init(positionKey: String = "KuroCat.Pos", defaultShiftX: CGFloat = 0) {
        self.positionKey = positionKey
        self.defaultShiftX = defaultShiftX
        let scale = CatSettings.shared.scale
        let size = CGSize(
            width: CatPhysics.canvasSize.width * scale,
            height: CatPhysics.canvasSize.height * scale
        )
        let origin = OverlayWindow.initialOrigin(for: size, key: positionKey, shiftX: defaultShiftX)

        super.init(
            contentRect: CGRect(origin: origin, size: size),
            styleMask: [.borderless, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )

        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        isMovable = false
        hidesOnDeactivate = false
        becomesKeyOnlyIfNeeded = true
        acceptsMouseMovedEvents = true
        ignoresMouseEvents = true

        applyWindowLevelAndBehavior()

        catView.frame = CGRect(origin: .zero, size: size)
        catView.autoresizingMask = [.width, .height]
        catView.positionKey = positionKey
        contentView = catView
    }

    func applyWindowLevelAndBehavior() {
        if CatSettings.shared.fullScreenOverlay {
            level = .statusBar
            collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .stationary, .ignoresCycle]
        } else {
            level = .floating
            collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
        }
    }

    func applyScale(_ newScale: CGFloat) {
        CatSettings.shared.scale = newScale
        let oldFrame = frame
        let newSize = CGSize(
            width: CatPhysics.canvasSize.width * newScale,
            height: CatPhysics.canvasSize.height * newScale
        )
        // Anchor bottom-center so scaling feels grounded
        let newOrigin = CGPoint(
            x: oldFrame.midX - newSize.width * 0.5,
            y: oldFrame.minY
        )
        setFrame(CGRect(origin: newOrigin, size: newSize), display: true)
        UserDefaults.standard.set(Double(newOrigin.x), forKey: positionKey + "X")
        UserDefaults.standard.set(Double(newOrigin.y), forKey: positionKey + "Y")
    }

    func resetPosition() {
        let origin = OverlayWindow.defaultOrigin(for: frame.size, shiftX: defaultShiftX)
        setFrameOrigin(origin)
        UserDefaults.standard.set(Double(origin.x), forKey: positionKey + "X")
        UserDefaults.standard.set(Double(origin.y), forKey: positionKey + "Y")
    }

    private static func initialOrigin(for size: CGSize, key: String, shiftX: CGFloat) -> CGPoint {
        let d = UserDefaults.standard
        if d.object(forKey: key + "X") != nil && d.object(forKey: key + "Y") != nil {
            let x = CGFloat(d.double(forKey: key + "X"))
            let y = CGFloat(d.double(forKey: key + "Y"))
            let saved = CGPoint(x: x, y: y)
            // Ensure saved origin is still on a connected display
            let rect = CGRect(origin: saved, size: size)
            if NSScreen.screens.contains(where: { $0.frame.intersects(rect) }) {
                return saved
            }
        }
        return defaultOrigin(for: size, shiftX: shiftX)
    }

    private static func defaultOrigin(for size: CGSize, shiftX: CGFloat = 0) -> CGPoint {
        let screen = NSScreen.main?.visibleFrame ?? CGRect(x: 0, y: 0, width: 1440, height: 900)
        return CGPoint(
            x: screen.maxX - size.width - 28 + shiftX,
            y: screen.minY + 18
        )
    }
}
