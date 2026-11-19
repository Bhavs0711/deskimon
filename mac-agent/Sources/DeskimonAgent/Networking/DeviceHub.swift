import Foundation
import Network
import DeskimonCore

/// Owns every transport to the Deskimon (Wi-Fi TCP + BLE), the heartbeat, and routes device messages into the
/// shared Assistant. Mac -> device pushes (results, alerts, emotions) all go through `send`.
@MainActor
final class DeviceHub: ObservableObject {
    @Published var wifiLinked = false
    @Published var bleLinked = false
    @Published var deviceID = ""
    @Published var firmware = ""
    @Published var hasToken = Keychain.load(KeychainKey.deviceToken) != nil
    @Published var pairingStatus = ""

    let server = DeviceServer()
    let ble = DeviceBLE()
    var assistant: Assistant!
    var settings: AppSettings!
    var speaker: MacSpeaker!
    var log: ((String) -> Void)?
    var onDeviceCommand: ((String, CommandResult) -> Void)?
    var onDismiss: (() -> Void)?

    private var tcpSession: DeviceSession?
    private var tcpConn: NWConnection?
    private var bleSession: DeviceSession?
    private var audio: [Int: Data] = [:]
    private var audioSeq: [Int: Int] = [:]      // tracks expected next seq to detect gaps
    private var audioTimer: [Int: Task<Void, Never>] = [:]  // per-stream timeout tasks
    private var hbTimer: Timer?
    private var everConnected = false

    var isConnected: Bool { wifiLinked || bleLinked }
    private var token: String? { Keychain.load(KeychainKey.deviceToken) }

    func start() {
        server.onState = { [weak self] s in Task { @MainActor in self?.log?(s) } }
        server.onConnection = { [weak self] c in Task { @MainActor in self?.accept(c) } }
        server.start()

        let bs = DeviceSession(transport: "ble", token: { [weak self] in self?.token }, send: { [weak self] d in Task { @MainActor in self?.ble.sendLine(d) } })
        bs.onAuthed = { [weak self] s in self?.authed(s) }
        bs.onMessage = { [weak self] s, m in self?.dispatch(s, m) }
        bleSession = bs
        ble.onLine = { [weak self] line in self?.bleSession?.receive(line) }
        ble.onConnectionChange = { [weak self] up in
            self?.log?(up ? "BLE connected" : "BLE disconnected")
            if !up { self?.bleLinked = false; self?.refreshState() }
        }
        ble.onProvisioned = { [weak self] in self?.pairingStatus = "Pairing data delivered. Deskimon is joining Wi-Fi..."; self?.log?("Provisioning delivered over BLE") }

        hbTimer = Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { [weak self] _ in Task { @MainActor in self?.heartbeat() } }
    }

    // MARK: pairing
    func pair(ssid: String, password: String) {
        let t = DeviceAuth.newPairingToken()
        Keychain.save(KeychainKey.deviceToken, t); hasToken = true
        pairingStatus = "Looking for Deskimon over Bluetooth (long-press its screen to enter pairing mode)..."
        ble.provision(ssid: ssid, password: password, token: t)
    }

    func unpair() {
        send(["type": "unpair"])
        Keychain.delete(KeychainKey.deviceToken); hasToken = false
        pairingStatus = "Unpaired."
    }

    // MARK: TCP
    private func accept(_ conn: NWConnection) {
        tcpConn?.cancel()
        tcpConn = conn
        let s = DeviceSession(transport: "wifi", token: { [weak self] in self?.token },
                              send: { d in conn.send(content: d, completion: .contentProcessed { _ in }) })
        s.onAuthed = { [weak self] s in self?.authed(s) }
        s.onMessage = { [weak self] s, m in self?.dispatch(s, m) }
        tcpSession = s
        conn.stateUpdateHandler = { [weak self] st in
            Task { @MainActor in
                guard let self, self.tcpConn === conn else { return }
                switch st {
                case .failed, .cancelled:
                    self.tcpSession = nil
                    self.wifiLinked = false
                    // Drop all in-flight audio buffers to prevent stale data on reconnect
                    self.audio.removeAll()
                    self.audioSeq.removeAll()
                    self.audioTimer.values.forEach { $0.cancel() }
                    self.audioTimer.removeAll()
                    self.refreshState()
                default: break
                }
            }
        }
        conn.start(queue: .global(qos: .userInitiated))
        var buffer = Data()
        func loop() {
            conn.receive(minimumIncompleteLength: 1, maximumLength: 16 * 1024) { data, _, done, error in
                if let data { buffer.append(data) }
                let lines = WireProtocol.drainLines(&buffer)
                if !lines.isEmpty { Task { @MainActor in lines.forEach { s.receive($0) } } }
                if done || error != nil { conn.cancel(); return }
                loop()
            }
        }
        loop()
    }

    private func authed(_ s: DeviceSession) {
        deviceID = s.deviceID; firmware = s.firmware
        if s.transport == "wifi" { wifiLinked = true } else { bleLinked = true }
        everConnected = true
        pairingStatus = "Paired and connected (\(s.transport))."
        log?("Deskimon \(s.deviceID) authenticated over \(s.transport) (fw \(s.firmware))")
        refreshState()
    }

    private func refreshState() {
        if !isConnected && everConnected {
            NotificationService.shared.post(title: "Deskimon disconnected", body: "I'll keep trying to reconnect.", sound: false)
        }
        ble.wantConnection = !wifiLinked
        if wifiLinked { ble.disconnect() } else { ble.scanIfNeeded() }
    }

    private func heartbeat() {
        let now = Date()
        for s in [tcpSession, bleSession].compactMap({ $0 }) where s.authed {
            if now.timeIntervalSince(s.lastRx) > 20 {
                log?("\(s.transport) heartbeat timeout")
                if s.transport == "wifi" { tcpConn?.cancel() } else { ble.disconnect() }
            } else { s.send(["type": "ping"]) }
        }
    }

    // MARK: outbound
    @discardableResult
    func send(_ obj: WireProtocol.JSON) -> Bool {
        if let s = tcpSession, s.authed { s.send(obj); return true }
        if let s = bleSession, s.authed { s.send(obj); return true }
        return false
    }

    func sendResult(_ r: CommandResult, id: Int?) {
        send(WireProtocol.resultMessage(id: id, r))
        send(["type": "overlay", "kind": r.overlay ?? "info", "title": r.title ?? "Deskimon",
              "subtitle": String((r.detail?.joined(separator: " - ") ?? r.message).prefix(90)), "duration_ms": 4500])
    }

    func sendState(_ s: String) { send(["type": "state", "state": s]) }
    func sendEmotion(_ e: String, hold: Int = 2500) { send(["type": "emotion", "emotion": DeskimonEmotion.sanitize(e), "hold_ms": hold]) }
    func sendAlert(kind: String, title: String, subtitle: String, holdMs: Int = 0) {
        send(["type": "alert", "kind": kind, "title": title, "subtitle": subtitle, "hold_ms": holdMs])
    }
    func sendDeadlineNear(title: String, subtitle: String) { send(["type": "deadline_near", "title": title, "subtitle": subtitle]) }

    // MARK: inbound
    private func dispatch(_ s: DeviceSession, _ m: WireProtocol.JSON) {
        guard let type = m["type"] as? String else { return }
        switch type {
        case "command":
            guard let text = m["text"] as? String else { return }
            let id = (m["id"] as? NSNumber)?.intValue
            Task { await runDeviceCommand(text, id: id) }
        case "audio_start":
            if let id = (m["id"] as? NSNumber)?.intValue {
                // Cancel any stale timeout for a previous stream with same id
                audioTimer[id]?.cancel(); audioTimer.removeValue(forKey: id)
                audio[id] = Data()
                audioSeq[id] = 0
                // Safety: auto-expire incomplete audio streams after 20 s
                let timeoutTask = Task { [weak self] in
                    try? await Task.sleep(nanoseconds: 20_000_000_000)
                    guard !Task.isCancelled else { return }
                    await MainActor.run { [weak self] in
                        guard let self, self.audio[id] != nil else { return }
                        self.log?("audio stream \(id) timed out — dropping")
                        self.audio.removeValue(forKey: id)
                        self.audioSeq.removeValue(forKey: id)
                        self.audioTimer.removeValue(forKey: id)
                        self.sendState("idle")
                    }
                }
                audioTimer[id] = timeoutTask
                if audio.count > 4 { audio.removeValue(forKey: audio.keys.min()!) }
            }
        case "audio":
            if let id = (m["id"] as? NSNumber)?.intValue,
               let b64 = m["data"] as? String,
               let d = Data(base64Encoded: b64),
               (audio[id]?.count ?? 0) < 16000 * 2 * 15 {
                // Warn on sequence gaps (dropped TCP chunks)
                if let seq = (m["seq"] as? NSNumber)?.intValue, let expected = audioSeq[id] {
                    if seq != expected { log?("audio id=\(id): seq gap expected \(expected) got \(seq)") }
                    audioSeq[id] = seq + 1
                }
                audio[id]?.append(d)
            }
        case "audio_end":
            guard let id = (m["id"] as? NSNumber)?.intValue,
                  let pcm = audio.removeValue(forKey: id) else { return }
            audioSeq.removeValue(forKey: id)
            audioTimer[id]?.cancel(); audioTimer.removeValue(forKey: id)
            Task { await runDeviceAudio(pcm, id: id) }
        case "event":
            if (m["event"] as? String) == "dismiss" { onDismiss?() }
        case "hb": s.send(["type": "hb_ack"])
        default: break
        }
    }

    private func runDeviceAudio(_ pcm: Data, id: Int) async {
        sendState("thinking")
        guard let text = await DeviceSTT.transcribe(pcm16: pcm), !text.isEmpty else {
            let r = CommandResult(success: false, message: "Sorry, I didn't catch that.", emotion: "confused", title: "Didn't hear you", overlay: "status")
            sendResult(r, id: id); await speakResult(r); return
        }
        log?("heard: \"\(text)\"")
        await runDeviceCommand(text, id: id)
    }

    func runDeviceCommand(_ text: String, id: Int?) async {
        let slow = Task { try? await Task.sleep(nanoseconds: 2_500_000_000); if !Task.isCancelled { sendState("processing") } }
        let r = await assistant.handle(text, origin: .deskimon)
        slow.cancel()
        sendResult(r, id: id)
        onDeviceCommand?(text, r)
        await speakResult(r)
    }

    /// Mirror a Mac-originated command on the device so both stay in sync.
    func mirrorMacCommand(running: Bool, result: CommandResult? = nil) {
        if running { sendState("thinking") } else if let result { sendResult(result, id: nil) }
    }

    func speakResult(_ r: CommandResult) async {
        if settings.speakOnMac { await speaker.speak(r.message) }
        if settings.speakOnDevice, tcpSession?.authed == true, (tcpSession?.caps["speaker"] as? Bool) == true {
            sendState("speaking")
            await streamTTS(r.message)
            // Signal device that playback is done so it re-enables wake detection
            sendState("idle")
        }
    }

    private func streamTTS(_ text: String) async {
        guard let pcm = await speaker.kokoro.pcm(text, rate: 16000), pcm.count < 16000 * 2 * 40 else { return }
        let chunk = 1024                      // 512 samples -> ~1.4 KB base64, fits the firmware's line buffer
        var off = 0
        while off < pcm.count {
            let end = min(off + chunk, pcm.count)
            let part = pcm.subdata(in: off..<end)
            send(["type": "tts", "rate": 16000, "data": part.base64EncodedString(), "last": end >= pcm.count])
            off = end
            try? await Task.sleep(nanoseconds: 12_000_000)
        }
    }
}
