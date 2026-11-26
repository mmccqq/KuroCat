import Foundation
import AppKit
import CryptoKit
import Carbon   // IsSecureEventInputEnabled

// Cat Link: two KuroCats show each other's cat. Each app sends small, anonymized activity events
// (which paw typed — never which key —, mouse movement, clicks, scrolls, headphones, petting) through
// a public MQTT server over secure WebSocket. Every message is encrypted with a key derived from the
// pairing code, so the server (and anyone else on it) only sees random bytes on a random topic.
// Wire format: docs/cat-link-protocol.md (the Windows version must match it).

// MARK: - Pairing code & encryption

/// A pairing code like "KC-C-7KQ2-M9XA-P3TD-W8RF-Z5HN". The letter after "KC" picks the relay server;
/// the 20 random characters (~99 bits) derive the encryption key and the private topic.
struct CatLinkCode: Equatable {
    enum Server: String, CaseIterable {
        case global = "G", china = "C"
        var host: String { self == .global ? "broker.emqx.io" : "broker-cn.emqx.io" }
        var title: String { self == .global ? "EMQX 国际（美国）" : "EMQX 中国（上海）" }
        var url: URL { URL(string: "wss://\(host):8084/mqtt")! }
    }

    static let alphabet = Array("23456789ABCDEFGHJKMNPQRSTUVWXYZ")   // no 0/O, 1/I/L
    let server: Server
    let secret: String   // 20 characters from `alphabet`

    static func generate(server: Server) -> CatLinkCode {
        var rng = SystemRandomNumberGenerator()
        let chars = (0..<20).map { _ in alphabet[Int(rng.next(upperBound: UInt32(alphabet.count)))] }
        return CatLinkCode(server: server, secret: String(chars))
    }

    /// Accepts the code with or without dashes/spaces, in any case.
    init?(parsing text: String) {
        let s = text.uppercased().filter { $0.isLetter || $0.isNumber }
        guard s.count == 23, s.hasPrefix("KC"), let server = Server(rawValue: String(s[s.index(s.startIndex, offsetBy: 2)])) else { return nil }
        let secret = String(s.suffix(20))
        guard secret.allSatisfy({ CatLinkCode.alphabet.contains($0) }) else { return nil }
        self.init(server: server, secret: secret)
    }

    init(server: Server, secret: String) {
        self.server = server
        self.secret = secret
    }

    var display: String {
        let groups = stride(from: 0, to: 20, by: 4).map { i -> String in
            let a = secret.index(secret.startIndex, offsetBy: i)
            return String(secret[a..<secret.index(a, offsetBy: 4)])
        }
        return "KC-\(server.rawValue)-" + groups.joined(separator: "-")
    }

    private func derive(_ info: String, _ count: Int) -> SymmetricKey {
        HKDF<SHA256>.deriveKey(inputKeyMaterial: SymmetricKey(data: Data(secret.utf8)),
                               salt: Data("kurocat-link-v1".utf8), info: Data(info.utf8), outputByteCount: count)
    }

    var key: SymmetricKey { derive("aes-gcm-key", 32) }
    var topic: String {
        "kurocat/v1/" + derive("mqtt-topic", 16).withUnsafeBytes { $0.map { String(format: "%02x", $0) }.joined() }
    }

    /// nonce (12) | ciphertext | tag (16) — CryptoKit's "combined" AES-GCM layout.
    func seal(_ plain: Data) -> Data? { try? AES.GCM.seal(plain, using: key).combined }
    func open(_ sealed: Data) -> Data? {
        guard let box = try? AES.GCM.SealedBox(combined: sealed) else { return nil }
        return try? AES.GCM.open(box, using: key)
    }
}

// MARK: - Wire messages

/// One event. Fields are short and optional to keep messages tiny; see docs/cat-link-protocol.md.
struct CatLinkEvent: Codable, Equatable {
    var k: String                 // kind: hello, kd, ku, m, c, w, pet, bye
    var o: Int? = nil             // milliseconds after the batch's first event
    var id: Int? = nil            // kd/ku: anonymous press id (not the key)
    var s: String? = nil          // kd: paw, "l" or "r" (viewer's left/right paw)
    var x: Double? = nil, y: Double? = nil      // m: cursor, 0...1 of the screen (y up); pet: canvas point
    var vx: Double? = nil, vy: Double? = nil    // m: velocity in screen-heights per second
    var b: Int? = nil             // c: button 0 left, 1 right, 2 other
    var d: Double? = nil          // c: 1 down / 0 up; w: scroll delta (points); pet: stroke length
    var h: Bool? = nil            // pet: on the head (else the body)
    var n: String? = nil          // hello: nickname
    var coat: String? = nil, kb: String? = nil, eye: String? = nil   // hello: theme names
    var hp: Bool? = nil, pl: Bool? = nil        // hello: headphones on, audio playing
}

struct CatLinkBatch: Codable {
    var v = 1
    var f: String                 // sender's random peer id
    var q: Int                    // sequence number
    var e: [CatLinkEvent]
}

// MARK: - Minimal MQTT 5 client over WebSocket (QoS 0 only)

final class MQTTWebSocketClient: NSObject, URLSessionWebSocketDelegate {
    var onConnected: (() -> Void)?
    var onClosed: ((String) -> Void)?
    var onMessage: ((String, Data) -> Void)?

    private let url: URL
    private let clientID: String
    private var session: URLSession?
    private var task: URLSessionWebSocketTask?
    private var buffer: [UInt8] = []
    private var pingTimer: Timer?
    private var lastReceive = Date()
    private var closed = false
    private(set) var isConnected = false
    private static let keepAlive: UInt16 = 60

    init(url: URL, clientID: String) {
        self.url = url
        self.clientID = clientID
    }

    func connect() {
        let session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: .main)
        self.session = session
        let task = session.webSocketTask(with: url, protocols: ["mqtt"])
        self.task = task
        task.resume()
        receiveLoop()
    }

    func disconnect() {
        guard !closed else { return }
        if isConnected { send([0xE0, 0x00]) }
        finish("disconnected")
        task?.cancel(with: .normalClosure, reason: nil)
    }

    func subscribe(_ topic: String) {
        // packet id 1, no properties, subscription options: QoS 0 + No Local (don't echo our own messages)
        let body: [UInt8] = [0x00, 0x01, 0x00] + Self.str(topic) + [0x04]
        send([0x82] + Self.varint(body.count) + body)
    }

    func publish(_ topic: String, _ payload: Data) {
        let body: [UInt8] = Self.str(topic) + [0x00] + [UInt8](payload)
        send([0x30] + Self.varint(body.count) + body)
    }

    // URLSessionWebSocketDelegate
    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask, didOpenWithProtocol protocol: String?) {
        let header: [UInt8] = Self.str("MQTT") + [0x05, 0x02, UInt8(Self.keepAlive >> 8), UInt8(Self.keepAlive & 0xFF), 0x00]
        let body = header + Self.str(clientID)
        send([0x10] + Self.varint(body.count) + body)
    }

    func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask,
                    didCloseWith closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?) {
        finish("closed (\(closeCode.rawValue))")
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        finish(error?.localizedDescription ?? "closed")
    }

    private func receiveLoop() {
        task?.receive { [weak self] result in
            guard let self, !self.closed else { return }
            switch result {
            case .success(.data(let data)):
                self.lastReceive = Date()
                self.buffer += data
                self.parsePackets()
                self.receiveLoop()
            case .success:
                self.receiveLoop()
            case .failure(let error):
                self.finish(error.localizedDescription)
            }
        }
    }

    private func parsePackets() {
        while buffer.count >= 2 {
            var length = 0, multiplier = 1, index = 1
            var byte: UInt8
            repeat {
                guard index < buffer.count, index <= 4 else { return }
                byte = buffer[index]
                length += Int(byte & 0x7F) * multiplier
                multiplier *= 128
                index += 1
            } while byte & 0x80 != 0
            guard buffer.count >= index + length else { return }
            let type = buffer[0] >> 4, flags = buffer[0] & 0x0F
            let body = Array(buffer[index..<(index + length)])
            buffer.removeFirst(index + length)
            handle(type: type, flags: flags, body: body)
        }
    }

    private func handle(type: UInt8, flags: UInt8, body: [UInt8]) {
        switch type {
        case 2: // CONNACK
            guard body.count >= 2, body[1] == 0 else {
                finish("server refused connection (\(body.count >= 2 ? body[1] : 255))"); return
            }
            isConnected = true
            pingTimer?.invalidate()
            pingTimer = Timer.scheduledTimer(withTimeInterval: 25, repeats: true) { [weak self] _ in
                guard let self else { return }
                if Date().timeIntervalSince(self.lastReceive) > Double(Self.keepAlive) * 1.5 {
                    self.finish("server stopped answering")
                    self.task?.cancel(with: .goingAway, reason: nil)
                } else {
                    self.send([0xC0, 0x00])   // PINGREQ
                }
            }
            onConnected?()
        case 3: // PUBLISH
            guard body.count >= 2 else { return }
            let topicLength = Int(body[0]) << 8 | Int(body[1])
            var i = 2 + topicLength
            guard body.count >= i else { return }
            let topic = String(decoding: body[2..<i], as: UTF8.self)
            if (flags >> 1) & 0x03 > 0 { i += 2 }            // packet id (QoS > 0)
            var propLength = 0, multiplier = 1
            while i < body.count {                              // skip properties
                let b = body[i]; i += 1
                propLength += Int(b & 0x7F) * multiplier; multiplier *= 128
                if b & 0x80 == 0 { break }
            }
            i += propLength
            guard i <= body.count else { return }
            onMessage?(topic, Data(body[i...]))
        case 14: // DISCONNECT from server
            finish("server closed the connection")
        default: // SUBACK, PINGRESP, ...
            break
        }
    }

    private func send(_ bytes: [UInt8]) {
        task?.send(.data(Data(bytes))) { [weak self] error in
            if let error { DispatchQueue.main.async { self?.finish(error.localizedDescription) } }
        }
    }

    private func finish(_ reason: String) {
        guard !closed else { return }
        closed = true
        isConnected = false
        pingTimer?.invalidate()
        session?.finishTasksAndInvalidate()
        onClosed?(reason)
    }

    private static func varint(_ value: Int) -> [UInt8] {
        var out: [UInt8] = [], v = value
        repeat {
            var byte = UInt8(v % 128); v /= 128
            if v > 0 { byte |= 0x80 }
            out.append(byte)
        } while v > 0
        return out
    }

    private static func str(_ s: String) -> [UInt8] {
        let u = Array(s.utf8)
        return [UInt8(u.count >> 8), UInt8(u.count & 0xFF)] + u
    }
}

// MARK: - Link session

final class CatLinkSession {
    enum Status: Equatable {
        case off, connecting, waitingForFriend, linked, friendOffline, reconnecting
    }

    static let shared = CatLinkSession()

    /// Status changed (main queue).
    var onStatusChange: (() -> Void)?
    /// An event from the friend, to be applied at `applyAt` (CACurrentMediaTime; main queue).
    var onFriendEvent: ((CatLinkEvent, CFTimeInterval) -> Void)?

    private(set) var status: Status = .off { didSet { if status != oldValue { onStatusChange?() } } }
    private(set) var code: CatLinkCode?
    private(set) var lastError: String?
    private(set) var friendName: String?

    let peerID: String
    private var client: MQTTWebSocketClient?
    private var pending: [CatLinkEvent] = []
    private var batchStart: CFTimeInterval = 0
    private var seq = 0
    private var timers: [Timer] = []
    private var retryDelay: TimeInterval = 2
    private var friendPeer: String?
    private var lastHeard: CFTimeInterval = 0
    private var pressIDs: [PhysicalKey: Int] = [:]
    private var nextPressID = 1
    private var lastMouseSent = CGPoint(x: -1, y: -1)
    private var mouseWasMoving = false

    /// What we tell the friend about our cat (refreshed whenever it changes).
    var profile: () -> CatLinkEvent = { CatLinkEvent(k: "hello") }

    init(peerID: String = UUID().uuidString.prefix(8).lowercased()) {
        self.peerID = String(peerID)
    }

    // MARK: Connection

    func start(code: CatLinkCode) {
        stop(sendBye: false)
        self.code = code
        retryDelay = 2
        status = .connecting
        startTimers()
        openClient()
    }

    func stop(sendBye: Bool = true) {
        if sendBye, client?.isConnected == true {
            pending.append(CatLinkEvent(k: "bye"))
            flush()
        }
        timers.forEach { $0.invalidate() }
        timers = []
        client?.onClosed = nil
        client?.disconnect()
        client = nil
        pending = []
        pressIDs = [:]
        friendPeer = nil
        friendName = nil
        status = .off
    }

    private func openClient() {
        guard let code else { return }
        let c = MQTTWebSocketClient(url: code.server.url, clientID: "kurocat-\(peerID)-\(Int.random(in: 1000...9999))")
        client = c
        c.onConnected = { [weak self] in
            guard let self else { return }
            NSLog("[CatLink] connected to %@", code.server.host)
            self.retryDelay = 2
            self.lastError = nil
            c.subscribe(code.topic)
            if self.friendPeer == nil { self.status = .waitingForFriend }
            else { self.status = CACurrentMediaTime() - self.lastHeard < 25 ? .linked : .friendOffline }
            self.sendHello()
        }
        c.onMessage = { [weak self] _, data in self?.receive(data) }
        c.onClosed = { [weak self] reason in
            guard let self, self.client === c else { return }
            NSLog("[CatLink] connection lost: %@ — retrying in %.0fs", reason, self.retryDelay)
            self.lastError = reason
            self.client = nil
            self.status = .reconnecting
            let delay = self.retryDelay
            self.retryDelay = min(60, self.retryDelay * 2)
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                guard let self, self.status == .reconnecting, self.client == nil else { return }
                self.openClient()
            }
        }
        c.connect()
    }

    private func startTimers() {
        let tick = { (interval: TimeInterval, block: @escaping () -> Void) -> Timer in
            let t = Timer(timeInterval: interval, repeats: true) { _ in block() }
            RunLoop.main.add(t, forMode: .common)
            return t
        }
        timers = [
            tick(0.1) { [weak self] in self?.sampleMouse(); self?.flush() },
            tick(10) { [weak self] in self?.sendHello() },
            tick(2) { [weak self] in self?.checkFriend() },
        ]
    }

    // MARK: Sending (called on the main queue)

    /// Our own key went down. Only the paw side is sent, and nothing while a password field is focused.
    func noteKeyDown(_ key: PhysicalKey) {
        guard isSending, !IsSecureEventInputEnabled() else { return }
        let id = nextPressID
        nextPressID = nextPressID % 9999 + 1
        pressIDs[key] = id
        queue(CatLinkEvent(k: "kd", id: id, s: key.prefersLeftPaw ? "l" : "r"))
    }

    func noteKeyUp(_ key: PhysicalKey) {
        guard isSending, let id = pressIDs.removeValue(forKey: key) else { return }
        queue(CatLinkEvent(k: "ku", id: id))
    }

    func noteClick(button: Int, isDown: Bool) {
        guard isSending else { return }
        queue(CatLinkEvent(k: "c", b: button, d: isDown ? 1 : 0))
    }

    func noteScroll(_ delta: CGFloat) {
        guard isSending else { return }
        let now = CACurrentMediaTime()
        // Trackpads send ~60 scroll events a second: merge them into one per 50 ms.
        if let last = pending.last, last.k == "w", let o = last.o, Double(o) >= (now - batchStart) * 1000 - 50 {
            pending[pending.count - 1].d = (last.d ?? 0) + Double(delta)
            return
        }
        queue(CatLinkEvent(k: "w", d: Double(delta)))
    }

    /// We petted the friend's cat on our screen: their own cat feels it too.
    func notePetFriendCat(canvasPoint: CGPoint, delta: CGFloat, isHead: Bool) {
        guard isSending else { return }
        queue(CatLinkEvent(k: "pet", x: round1(canvasPoint.x), y: round1(canvasPoint.y), d: round1(delta), h: isHead))
    }

    /// Our nickname, themes or headphones changed.
    func profileChanged() { sendHello() }

    private var isSending: Bool { client?.isConnected == true }

    private func sendHello() {
        guard isSending else { return }
        queue(profile())
        flush()
    }

    private func sampleMouse() {
        guard isSending else { return }
        let engine = InputEngine.shared
        let loc = engine.lastMouseLocation
        let screen = (NSScreen.screens.first { $0.frame.contains(loc) } ?? NSScreen.main)?.frame
            ?? CGRect(x: 0, y: 0, width: 1440, height: 900)
        let v = engine.mouseVelocity
        let moving = hypot(v.dx, v.dy) > 20
        let moved = hypot(loc.x - lastMouseSent.x, loc.y - lastMouseSent.y) > 0.5
        guard moving || moved || mouseWasMoving else { return }
        mouseWasMoving = moving
        lastMouseSent = loc
        queue(CatLinkEvent(k: "m",
                           x: round3((loc.x - screen.minX) / screen.width), y: round3((loc.y - screen.minY) / screen.height),
                           vx: moving ? round3(v.dx / screen.height) : 0, vy: moving ? round3(v.dy / screen.height) : 0))
    }

    private func queue(_ event: CatLinkEvent) {
        let now = CACurrentMediaTime()
        if pending.isEmpty { batchStart = now }
        var e = event
        e.o = Int((now - batchStart) * 1000)
        pending.append(e)
        if pending.count >= 40 { flush() }
    }

    private func flush() {
        guard !pending.isEmpty, let client, client.isConnected, let code else { return }
        seq += 1
        let batch = CatLinkBatch(f: peerID, q: seq, e: pending)
        pending = []
        guard let json = try? JSONEncoder().encode(batch), let sealed = code.seal(json) else { return }
        client.publish(code.topic, sealed)
    }

    // MARK: Receiving

    private func receive(_ data: Data) {
        guard let code, let plain = code.open(data),
              let batch = try? JSONDecoder().decode(CatLinkBatch.self, from: plain),
              batch.v == 1, batch.f != peerID else { return }
        let now = CACurrentMediaTime()
        if batch.e.contains(where: { $0.k == "bye" }) {
            if batch.f == friendPeer { lastHeard = 0; checkFriend() }
            return
        }
        if friendPeer != batch.f {
            let isNew = friendPeer == nil
            friendPeer = batch.f
            NSLog("[CatLink] friend joined")
            if isNew { sendHello() }   // let a newly arrived friend see our cat right away
        }
        lastHeard = now
        if status != .linked { status = .linked }
        // A small buffer keeps the friend's rhythm even if batches arrive unevenly.
        for event in batch.e {
            if event.k == "hello" { friendName = event.n }
            onFriendEvent?(event, now + 0.12 + Double(event.o ?? 0) / 1000)
        }
    }

    private func checkFriend() {
        guard status == .linked else { return }
        if CACurrentMediaTime() - lastHeard > 25 {
            NSLog("[CatLink] friend went offline")
            status = .friendOffline
        }
    }

    private func round1(_ v: CGFloat) -> Double { (Double(v) * 10).rounded() / 10 }
    private func round3(_ v: CGFloat) -> Double { (Double(v) * 1000).rounded() / 1000 }
}

// MARK: - Theme names on the wire

extension CatLinkEvent {
    static func themeName<T>(_ value: T) -> String { String(describing: value) }
    static func theme<T: CaseIterable>(_ name: String?, _ type: T.Type) -> T? {
        guard let name else { return nil }
        return T.allCases.first { String(describing: $0) == name }
    }
}

// MARK: - Remote cat driver

/// Turns the friend's events into frame input for the second cat. Their keys arrive anonymized
/// (just "left paw" / "right paw"), so each press lands on a random key on that side; key bubbles
/// show that random stand-in, never what the friend actually typed.
final class RemoteCatDriver {
    let physics: CatPhysics
    private(set) var hasProfile = false
    private var heldKeys: [Int: (key: PhysicalKey, since: CFTimeInterval)] = [:]
    private var order: [Int] = []
    private var keyTimes: [CFTimeInterval] = []
    private var mousePos = CGPoint(x: 0.5, y: 0.5)        // normalized, y up
    private var mouseVel = CGVector.zero                  // screen-heights per second
    private var lastMouseSample: CFTimeInterval = 0
    private var leftDown = false, rightDown = false
    private var lastMove: CFTimeInterval = 0, lastClick: CFTimeInterval = 0, lastKey: CFTimeInterval = 0

    private static let typingKeys = PhysicalKey.allCases.filter {
        $0.visualKey == $0 && ![.leftShift, .rightShift, .leftControl, .leftOption, .leftCommand, .rightCommand,
                                .rightOption, .fn, .capsLock, .tab, .escape, .enter, .backspace,
                                .arrowUp, .arrowDown, .arrowLeft, .arrowRight].contains($0)
    }
    private static let leftPawKeys = typingKeys.filter { $0.prefersLeftPaw }
    private static let rightPawKeys = typingKeys.filter { !$0.prefersLeftPaw }

    init(physics: CatPhysics) {
        self.physics = physics
        physics.externalActiveKeys = []
        physics.kpmOverride = 0
    }

    func apply(_ e: CatLinkEvent, now: CFTimeInterval) {
        switch e.k {
        case "hello":
            if let coat = CatLinkEvent.theme(e.coat, CatCoatTheme.self) {
                physics.appearance = CatPhysics.Appearance(
                    coat: coat,
                    keyboard: CatLinkEvent.theme(e.kb, KeyboardTheme.self) ?? .classicMilk,
                    eye: CatLinkEvent.theme(e.eye, EyeColorTheme.self) ?? .neonChartreuse)
            }
            physics.setHeadphonesConnected(e.hp ?? false, animated: hasProfile)
            physics.headphonesPlaying = e.pl ?? false
            hasProfile = true
        case "kd":
            guard let id = e.id else { return }
            let pool = e.s == "l" ? Self.leftPawKeys : Self.rightPawKeys
            let held = Set(heldKeys.values.map(\.key))
            let key = pool.filter { !held.contains($0) }.randomElement() ?? pool.randomElement()!
            heldKeys[id] = (key, now)
            order.append(id)
            keyTimes.append(now)
            lastKey = now
            syncKeys()
            physics.triggerKeyDown(key, at: now)
        case "ku":
            guard let id = e.id, heldKeys.removeValue(forKey: id) != nil else { return }
            order.removeAll { $0 == id }
            lastKey = now
            syncKeys()
        case "m":
            mousePos = CGPoint(x: e.x ?? mousePos.x, y: e.y ?? mousePos.y)
            mouseVel = CGVector(dx: e.vx ?? 0, dy: e.vy ?? 0)
            lastMouseSample = now
            lastMove = now
        case "c":
            let down = (e.d ?? 0) > 0.5
            if e.b == 1 { rightDown = down } else if e.b == 0 { leftDown = down }
            if down { lastClick = now }
            physics.triggerMouseClick(button: e.b ?? 0, isDown: down)
        case "w":
            physics.triggerScroll(delta: CGFloat(e.d ?? 0), at: now)
            lastMove = now
        default:
            break
        }
    }

    /// The friend went away: let go of everything so no key stays pressed.
    func releaseAll() {
        heldKeys = [:]
        order = []
        leftDown = false
        rightDown = false
        mouseVel = .zero
        syncKeys()
    }

    func frameInput(now: CFTimeInterval, screen: CGRect) -> ScriptedFrameInput {
        // Missed key-ups (lost messages) shouldn't leave a paw stuck forever.
        if heldKeys.contains(where: { now - $0.value.since > 20 }) {
            for (id, held) in heldKeys where now - held.since > 20 { heldKeys[id] = nil; order.removeAll { $0 == id } }
            syncKeys()
        }
        if now - lastMouseSample > 0.3 { mouseVel = CGVector(dx: mouseVel.dx * 0.8, dy: mouseVel.dy * 0.8) }
        keyTimes.removeAll { now - $0 > 10 }
        physics.kpmOverride = keyTimes.count * 6
        let ordered = order.compactMap { heldKeys[$0]?.key }
        return ScriptedFrameInput(
            now: now, activeKeys: Set(ordered), orderedKeys: ordered,
            mouseScreenPos: CGPoint(x: screen.minX + mousePos.x * screen.width, y: screen.minY + mousePos.y * screen.height),
            mouseVelocity: CGVector(dx: mouseVel.dx * screen.height, dy: mouseVel.dy * screen.height),
            leftMouseDown: leftDown, rightMouseDown: rightDown,
            lastMouseMoveTime: lastMove, lastMouseClickTime: lastClick, lastKeyActivityTime: lastKey)
    }

    private func syncKeys() {
        physics.externalActiveKeys = Set(heldKeys.values.map(\.key))
    }
}
