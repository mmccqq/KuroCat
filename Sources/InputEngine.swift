import Foundation
import AppKit
import IOKit
import IOKit.hid
import ApplicationServices

enum InputEngineStatus: String {
    case fullHardware = "硬件级全状态监听中 (HID + EventTap)"
    case hidOnly = "硬件 HID 监听中 (游戏/打字全支持)"
    case eventTapOnly = "全局事件监听中 (EventTap)"
    case needsPermission = "需开启「输入监控」以全局监听键盘"

    var displayName: String {
        switch self {
        case .fullHardware: return tr(rawValue, "Hardware-level listening (HID + EventTap)")
        case .hidOnly: return tr(rawValue, "Hardware HID listening (typing and games)")
        case .eventTapOnly: return tr(rawValue, "Global event listening (EventTap)")
        case .needsPermission: return tr(rawValue, "Allow Input Monitoring to see the keyboard everywhere")
        }
    }
}

/// Dual-layer full-state keyboard & mouse capture engine for macOS.
/// Layer 1: IOHIDManager (kernel HID — true NKRO, works in games, IME, modifiers, long holds).
/// Layer 2: CGEventTap (.listenOnly — catches trackpad taps, virtual/remapped keys, scroll).
/// Layer 3: Local NSEvent + mouseLocation (always active even before permissions).
final class InputEngine {
    static let shared = InputEngine()

    // Unified active state
    private var hidPressedKeys = Set<PhysicalKey>()
    private var tapPressedKeys = Set<PhysicalKey>()
    private(set) var activeKeys = Set<PhysicalKey>()
    private(set) var pressedOrder: [PhysicalKey] = []

    // Mouse state
    private(set) var leftMouseDown = false
    private(set) var rightMouseDown = false
    private(set) var otherMouseDown = false
    private(set) var lastMouseMoveTime: CFTimeInterval = 0
    private(set) var lastMouseClickTime: CFTimeInterval = 0
    private(set) var lastKeyActivityTime: CFTimeInterval = 0
    private(set) var lastMouseLocation: CGPoint = NSEvent.mouseLocation
    private(set) var mouseVelocity: CGVector = .zero

    // Stats
    private(set) var totalKeystrokesToday: Int = 0
    private var recentKeyTimes: [CFTimeInterval] = []
    private var statsDayString: String = ""

    // Engine handles
    private var hidManager: IOHIDManager?
    private var eventTap: CFMachPort?
    private var eventTapRunLoopSource: CFRunLoopSource?
    private var localMonitor: Any?
    private var permissionCheckTimer: Timer?

    private(set) var isHIDActive = false
    private(set) var isTapActive = false

    // Callbacks
    var onKeyDown: ((PhysicalKey) -> Void)?
    var onKeyUp: ((PhysicalKey) -> Void)?
    var onMouseClick: ((Int, Bool) -> Void)?   // button (0=L,1=R,2=Other), isDown
    var onScroll: ((CGFloat) -> Void)?         // scroll delta in points (vertical, or horizontal if no vertical)
    var onStatusChanged: ((InputEngineStatus) -> Void)?

    private var lastModifierFlags: CGEventFlags = []

    var status: InputEngineStatus {
        guard hasKeyboardGlobalPermission else { return .needsPermission }
        if isHIDActive && isTapActive { return .fullHardware }
        if isHIDActive { return .hidOnly }
        if isTapActive { return .eventTapOnly }
        return .needsPermission
    }

    var hasInputMonitoringPermission: Bool {
        CGPreflightListenEventAccess() || (IOHIDCheckAccess(kIOHIDRequestTypeListenEvent) == kIOHIDAccessTypeGranted)
    }

    var hasAccessibilityPermission: Bool {
        AXIsProcessTrusted()
    }

    var hasKeyboardGlobalPermission: Bool {
        hasInputMonitoringPermission || hasAccessibilityPermission
    }

    var currentKPM: Int {
        let now = CACurrentMediaTime()
        let cutoff = now - 10.0
        let count = recentKeyTimes.filter { $0 >= cutoff }.count
        return count * 6
    }

    private init() {
        loadStats()
    }

    func start() {
        startLocalFallbackMonitor()
        tryStartHID()
        tryStartEventTap()
        notifyStatus()

        // Periodically check if the user just granted Input Monitoring permission
        permissionCheckTimer?.invalidate()
        permissionCheckTimer = Timer.scheduledTimer(withTimeInterval: 1.5, repeats: true) { [weak self] _ in
            guard let self else { return }
            let prevStatus = self.status
            if !self.isHIDActive && self.hasInputMonitoringPermission {
                self.tryStartHID()
            }
            if !self.isTapActive && (self.hasInputMonitoringPermission || self.hasAccessibilityPermission) {
                self.tryStartEventTap()
            } else if self.isTapActive, let tap = self.eventTap, !CGEvent.tapIsEnabled(tap: tap) {
                CGEvent.tapEnable(tap: tap, enable: true)
            }
            if self.status != prevStatus {
                self.notifyStatus()
            }
        }
    }

    func stop() {
        permissionCheckTimer?.invalidate()
        permissionCheckTimer = nil
        stopHID()
        stopEventTap()
        if let m = localMonitor {
            NSEvent.removeMonitor(m)
            localMonitor = nil
        }
    }

    /// Request macOS Input Monitoring permission and open System Settings directly.
    func requestPermissionsAndOpenSettings() {
        CGRequestListenEventAccess()
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent)
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent") {
            NSWorkspace.shared.open(url)
        }
    }

    func openAccessibilitySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }

    /// Called at 60fps from the render loop to sample global cursor motion (requires zero permissions).
    func pollMouse(dt: CGFloat) {
        let loc = NSEvent.mouseLocation
        let dx = loc.x - lastMouseLocation.x
        let dy = loc.y - lastMouseLocation.y
        let dist = hypot(dx, dy)
        if dist > 0.4 {
            lastMouseMoveTime = CACurrentMediaTime()
            if dt > 0.001 {
                mouseVelocity = CGVector(dx: dx / dt, dy: dy / dt)
            }
        } else {
            mouseVelocity = CGVector(dx: mouseVelocity.dx * 0.82, dy: mouseVelocity.dy * 0.82)
        }
        lastMouseLocation = loc

        // Keep KPM sliding window clean
        let now = CACurrentMediaTime()
        if recentKeyTimes.count > 120 {
            recentKeyTimes.removeAll { now - $0 > 12.0 }
        }
        releaseStuckKeys(now: now)
    }

    // MARK: - Stuck keys

    /// When each held key went down.
    private var keyDownSince: [PhysicalKey: CFTimeInterval] = [:]
    private var lastStuckKeyCheck: CFTimeInterval = 0

    /// Every macOS key code of each key (Return and keypad Enter are both `.enter`).
    private static let cgKeyCodes: [PhysicalKey: [CGKeyCode]] = {
        var map: [PhysicalKey: [CGKeyCode]] = [:]
        for code in 0..<128 {
            if let key = PhysicalKey.fromCGKeyCode(UInt16(code)) { map[key, default: []].append(CGKeyCode(code)) }
        }
        return map
    }()

    /// Keys held at least this long are compared with the keyboard (a newer key-up may still be on its way).
    static let stuckKeyGrace: CFTimeInterval = 0.4

    /// Held keys that the keyboard says are no longer down. A key-up can go missing: while the screen is
    /// locked, or a password field turns on Secure Input, key releases go to macOS only (an event tap that
    /// timed out misses them too). Without this check, the paw stays on that key until it is pressed again.
    /// Caps Lock is also released here: the event tap reports it as held for as long as Caps Lock is on.
    static func stuckKeys(active: Set<PhysicalKey>, downSince: [PhysicalKey: CFTimeInterval], now: CFTimeInterval,
                          isDown: (CGKeyCode) -> Bool) -> Set<PhysicalKey> {
        active.filter { key in
            guard now - (downSince[key] ?? now) >= stuckKeyGrace, let codes = cgKeyCodes[key] else { return false }
            return !codes.contains(where: isDown)
        }
    }

    /// Four times a second: release held keys that aren't physically down (the Windows version does the same).
    private func releaseStuckKeys(now: CFTimeInterval) {
        guard !activeKeys.isEmpty, now - lastStuckKeyCheck >= 0.25 else { return }
        lastStuckKeyCheck = now
        let stuck = Self.stuckKeys(active: activeKeys, downSince: keyDownSince, now: now,
                                   isDown: { CGEventSource.keyState(.hidSystemState, key: $0) })
        for key in stuck {
            hidPressedKeys.remove(key)
            tapPressedKeys.remove(key)
            recomputeUnifiedKeys()
            keyDownSince[key] = nil
            pressedOrder.removeAll { $0 == key }
            onKeyUp?(key)
        }
    }

    // MARK: - Layer 1: IOHIDManager (Raw Kernel HID — Best for Games + IME)

    private func tryStartHID() {
        guard !isHIDActive else { return }
        guard hasInputMonitoringPermission else { return }

        let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))

        let matches: [[String: Any]] = [
            [
                kIOHIDDeviceUsagePageKey as String: kHIDPage_GenericDesktop,
                kIOHIDDeviceUsageKey as String: kHIDUsage_GD_Keyboard
            ],
            [
                kIOHIDDeviceUsagePageKey as String: kHIDPage_GenericDesktop,
                kIOHIDDeviceUsageKey as String: kHIDUsage_GD_Keypad
            ],
            [
                kIOHIDDeviceUsagePageKey as String: kHIDPage_GenericDesktop,
                kIOHIDDeviceUsageKey as String: kHIDUsage_GD_Mouse
            ]
        ]

        IOHIDManagerSetDeviceMatchingMultiple(manager, matches as CFArray)

        let context = Unmanaged.passUnretained(self).toOpaque()
        IOHIDManagerRegisterInputValueCallback(manager, { ctx, _, _, value in
            guard let ctx else { return }
            let engine = Unmanaged<InputEngine>.fromOpaque(ctx).takeUnretainedValue()
            engine.handleHIDValue(value)
        }, context)

        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
        let openResult = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
        if openResult == kIOReturnSuccess {
            self.hidManager = manager
            self.isHIDActive = true
        } else {
            IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
            self.isHIDActive = false
        }
    }

    private func stopHID() {
        if let manager = hidManager {
            IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
            IOHIDManagerClose(manager, IOOptionBits(kIOHIDOptionsTypeNone))
            hidManager = nil
        }
        isHIDActive = false
        hidPressedKeys.removeAll()
        recomputeUnifiedKeys()
    }

    private func handleHIDValue(_ value: IOHIDValue) {
        let element = IOHIDValueGetElement(value)
        let usagePage = IOHIDElementGetUsagePage(element)
        let usage = IOHIDElementGetUsage(element)
        let intVal = IOHIDValueGetIntegerValue(value)

        // Standard Keyboard / Keypad page (0x07)
        if usagePage == UInt32(kHIDPage_KeyboardOrKeypad) {
            // Filter out HID error/rollover pseudo-usages (0x00..0x03)
            guard usage >= 0x04 else { return }
            if let key = PhysicalKey.fromHIDUsage(usage) {
                setKeyState(key, isDown: intVal != 0, fromHID: true)
            }
            return
        }

        // Apple Built-in / Magic Keyboard Fn key (Apple Vendor Top Case page 0xFF, usage 0x03)
        if usagePage == 0xFF && usage == 0x03 {
            setKeyState(.fn, isDown: intVal != 0, fromHID: true)
            return
        }

        // Mouse buttons (Page 0x09)
        if usagePage == UInt32(kHIDPage_Button) {
            let isDown = intVal != 0
            if usage == 1 {
                updateMouseButton(0, isDown: isDown)
            } else if usage == 2 {
                updateMouseButton(1, isDown: isDown)
            } else if usage >= 3 && usage <= 8 {
                updateMouseButton(2, isDown: isDown)
            }
        }
    }

    // MARK: - Layer 2: CGEventTap (Listen-Only Companion for Trackpad, Virtual Keys & Scroll)

    private func tryStartEventTap() {
        guard !isTapActive else { return }
        // Only activate keyboard EventTap when macOS actually granted Input Monitoring or Accessibility;
        // otherwise listenOnly tap silently drops keyboard events while returning non-nil!
        guard hasKeyboardGlobalPermission else { return }

        var mask: CGEventMask = 0
        let types: [CGEventType] = [
            .keyDown, .keyUp, .flagsChanged,
            .leftMouseDown, .leftMouseUp,
            .rightMouseDown, .rightMouseUp,
            .otherMouseDown, .otherMouseUp,
            .scrollWheel
        ]
        for t in types {
            mask |= (1 << t.rawValue)
        }

        let context = Unmanaged.passUnretained(self).toOpaque()
        guard let tap = CGEvent.tapCreate(
            tap: .cgSessionEventTap,
            place: .headInsertEventTap,
            options: .listenOnly,
            eventsOfInterest: mask,
            callback: { _, type, event, ctx in
                guard let ctx else { return Unmanaged.passUnretained(event) }
                let engine = Unmanaged<InputEngine>.fromOpaque(ctx).takeUnretainedValue()
                engine.handleCGEvent(type: type, event: event)
                return Unmanaged.passUnretained(event)
            },
            userInfo: context
        ) else {
            isTapActive = false
            return
        }

        let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)

        self.eventTap = tap
        self.eventTapRunLoopSource = source
        self.isTapActive = true
    }

    private func stopEventTap() {
        if let tap = eventTap {
            CGEvent.tapEnable(tap: tap, enable: false)
        }
        if let source = eventTapRunLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, .commonModes)
        }
        eventTap = nil
        eventTapRunLoopSource = nil
        isTapActive = false
        tapPressedKeys.removeAll()
        recomputeUnifiedKeys()
    }

    private func handleCGEvent(type: CGEventType, event: CGEvent) {
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            if let tap = eventTap {
                CGEvent.tapEnable(tap: tap, enable: true)
            }
            return
        }

        switch type {
        case .keyDown:
            // Ignore OS auto-repeat events so holding W in a game doesn't spam floating keycaps!
            let isRepeat = event.getIntegerValueField(.keyboardEventAutorepeat) != 0
            let keyCode = UInt16(event.getIntegerValueField(.keyboardEventKeycode))
            if let key = PhysicalKey.fromCGKeyCode(keyCode) {
                if !isRepeat {
                    setKeyState(key, isDown: true, fromHID: false)
                }
            }
        case .keyUp:
            let keyCode = UInt16(event.getIntegerValueField(.keyboardEventKeycode))
            if let key = PhysicalKey.fromCGKeyCode(keyCode) {
                setKeyState(key, isDown: false, fromHID: false)
            }
        case .flagsChanged:
            let keyCode = UInt16(event.getIntegerValueField(.keyboardEventKeycode))
            let flags = event.flags
            if let key = PhysicalKey.fromCGKeyCode(keyCode) {
                let isDown = isModifierKeyDown(key: key, flags: flags)
                setKeyState(key, isDown: isDown, fromHID: false)
            }
            lastModifierFlags = flags
        case .leftMouseDown:
            updateMouseButton(0, isDown: true)
        case .leftMouseUp:
            updateMouseButton(0, isDown: false)
        case .rightMouseDown:
            updateMouseButton(1, isDown: true)
        case .rightMouseUp:
            updateMouseButton(1, isDown: false)
        case .otherMouseDown:
            updateMouseButton(2, isDown: true)
        case .otherMouseUp:
            updateMouseButton(2, isDown: false)
        case .scrollWheel:
            lastMouseMoveTime = CACurrentMediaTime()
            // Skip trackpad inertia (momentum) events so the paw stops when the fingers lift.
            guard event.getIntegerValueField(.scrollWheelEventMomentumPhase) == 0 else { break }
            var delta = event.getDoubleValueField(.scrollWheelEventPointDeltaAxis1)
            if delta == 0 { delta = event.getDoubleValueField(.scrollWheelEventPointDeltaAxis2) }
            if delta != 0 { onScroll?(CGFloat(delta)) }
        default:
            break
        }
    }

    private func isModifierKeyDown(key: PhysicalKey, flags: CGEventFlags) -> Bool {
        switch key {
        case .leftShift, .rightShift: return flags.contains(.maskShift)
        case .leftControl, .rightControl: return flags.contains(.maskControl)
        case .leftOption, .rightOption: return flags.contains(.maskAlternate)
        case .leftCommand, .rightCommand: return flags.contains(.maskCommand)
        case .capsLock: return flags.contains(.maskAlphaShift)
        case .fn: return flags.contains(.maskSecondaryFn)
        default: return false
        }
    }

    // MARK: - Layer 3: Local Window Monitor (Immediate testing before permissions)

    private func startLocalFallbackMonitor() {
        localMonitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { [weak self] event in
            guard let self else { return event }
            if !self.isHIDActive && !self.isTapActive {
                if event.type == .keyDown && !event.isARepeat {
                    if let key = PhysicalKey.fromCGKeyCode(event.keyCode) {
                        self.setKeyState(key, isDown: true, fromHID: false)
                    }
                } else if event.type == .keyUp {
                    if let key = PhysicalKey.fromCGKeyCode(event.keyCode) {
                        self.setKeyState(key, isDown: false, fromHID: false)
                    }
                } else if event.type == .flagsChanged {
                    if let key = PhysicalKey.fromCGKeyCode(event.keyCode) {
                        let cgFlags = CGEventFlags(rawValue: UInt64(event.modifierFlags.rawValue))
                        self.setKeyState(key, isDown: self.isModifierKeyDown(key: key, flags: cgFlags), fromHID: false)
                    }
                }
            }
            return event
        }
    }

    // MARK: - Unified Deduplicated State

    private func setKeyState(_ key: PhysicalKey, isDown: Bool, fromHID: Bool) {
        let wasActive = activeKeys.contains(key)

        if fromHID {
            if isDown {
                hidPressedKeys.insert(key)
            } else {
                hidPressedKeys.remove(key)
                // If HID explicitly says the physical key was released, clear any lingering tap state too
                tapPressedKeys.remove(key)
            }
        } else {
            if isDown {
                tapPressedKeys.insert(key)
            } else {
                tapPressedKeys.remove(key)
                if !isHIDActive {
                    hidPressedKeys.remove(key)
                }
            }
        }

        recomputeUnifiedKeys()
        let isNowActive = activeKeys.contains(key)

        if !wasActive && isNowActive {
            let now = CACurrentMediaTime()
            keyDownSince[key] = now
            lastKeyActivityTime = now
            recentKeyTimes.append(now)
            incrementTodayStats()
            pressedOrder.removeAll { $0 == key }
            pressedOrder.append(key)
            onKeyDown?(key)
        } else if wasActive && !isNowActive {
            keyDownSince[key] = nil
            pressedOrder.removeAll { $0 == key }
            onKeyUp?(key)
        }
    }

    private func recomputeUnifiedKeys() {
        activeKeys = hidPressedKeys.union(tapPressedKeys)
    }

    private func updateMouseButton(_ button: Int, isDown: Bool) {
        let prev: Bool
        switch button {
        case 0:
            prev = leftMouseDown
            leftMouseDown = isDown
        case 1:
            prev = rightMouseDown
            rightMouseDown = isDown
        default:
            prev = otherMouseDown
            otherMouseDown = isDown
        }
        if prev != isDown {
            let now = CACurrentMediaTime()
            lastMouseMoveTime = now
            if isDown { lastMouseClickTime = now }
            onMouseClick?(button, isDown)
        }
    }

    private func notifyStatus() {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.onStatusChanged?(self.status)
        }
    }

    // MARK: - Daily Stats Persistence

    private func todayString() -> String {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd"
        return f.string(from: Date())
    }

    private func loadStats() {
        let today = todayString()
        statsDayString = today
        let savedDay = UserDefaults.standard.string(forKey: "Whiskerkey.StatsDay") ?? ""
        if savedDay == today {
            totalKeystrokesToday = UserDefaults.standard.integer(forKey: "Whiskerkey.TodayKeys")
        } else {
            totalKeystrokesToday = 0
            UserDefaults.standard.set(today, forKey: "Whiskerkey.StatsDay")
            UserDefaults.standard.set(0, forKey: "Whiskerkey.TodayKeys")
        }
    }

    private func incrementTodayStats() {
        let today = todayString()
        if today != statsDayString {
            statsDayString = today
            totalKeystrokesToday = 0
            UserDefaults.standard.set(today, forKey: "Whiskerkey.StatsDay")
        }
        totalKeystrokesToday += 1
        if totalKeystrokesToday % 10 == 0 {
            UserDefaults.standard.set(totalKeystrokesToday, forKey: "Whiskerkey.TodayKeys")
        }
    }

    func flushStats() {
        UserDefaults.standard.set(statsDayString, forKey: "Whiskerkey.StatsDay")
        UserDefaults.standard.set(totalKeystrokesToday, forKey: "Whiskerkey.TodayKeys")
    }
}
