import Foundation
import DeskimonCore

/// One authenticated conversation with the device on one transport (Wi-Fi TCP or BLE).
/// Handles the mutual HMAC handshake; everything after auth is forwarded to the hub.
@MainActor
final class DeviceSession {
    let transport: String                      // "wifi" | "ble"
    private(set) var authed = false
    private(set) var deviceID = ""
    private(set) var firmware = ""
    private(set) var caps: [String: Any] = [:]
    var lastRx = Date()
    private var macNonce = ""
    private var devNonce = ""
    private let sendData: (Data) -> Void
    private let tokenProvider: () -> String?
    var onAuthed: ((DeviceSession) -> Void)?
    var onMessage: ((DeviceSession, WireProtocol.JSON) -> Void)?

    init(transport: String, token: @escaping () -> String?, send: @escaping (Data) -> Void) {
        self.transport = transport; self.tokenProvider = token; self.sendData = send
    }

    func send(_ obj: WireProtocol.JSON) { if let d = WireProtocol.encode(obj) { sendData(d) } }

    func receive(_ line: Data) {
        lastRx = Date()
        guard let m = WireProtocol.decode(line), let type = m["type"] as? String else { return }
        switch type {
        case "hello":
            authed = false
            devNonce = m["nonce"] as? String ?? ""
            deviceID = m["id"] as? String ?? ""; firmware = m["fw"] as? String ?? ""; caps = m["caps"] as? [String: Any] ?? [:]
            macNonce = DeviceAuth.randomHex(bytes: 16)
            send(["type": "challenge", "nonce": macNonce])
        case "auth":
            guard let token = tokenProvider(), !macNonce.isEmpty, let h = m["hmac"] as? String,
                  DeviceAuth.constantTimeEqual(h, DeviceAuth.hmacHex(token: token, message: macNonce)) else {
                send(["type": "error", "message": "auth failed"]); return
            }
            authed = true
            let tz = TimeZone.current.secondsFromGMT() / 60
            send(["type": "welcome", "hmac": DeviceAuth.hmacHex(token: token, message: devNonce),
                  "epoch": Int(Date().timeIntervalSince1970), "tz_offset_min": tz, "name": Host.current().localizedName ?? "Mac"])
            onAuthed?(self)
        default:
            if authed { onMessage?(self, m) }
        }
    }
}
