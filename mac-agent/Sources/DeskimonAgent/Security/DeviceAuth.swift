import Foundation
import CryptoKit

/// Mutual HMAC-SHA256 challenge/response used on both Wi-Fi and BLE. The shared token is generated on the Mac,
/// delivered to the device once over BLE while the device is in pairing mode, and stored in the Keychain.
enum DeviceAuth {
    static func hmacHex(token: String, message: String) -> String {
        let key = SymmetricKey(data: Data(token.utf8))
        let mac = HMAC<SHA256>.authenticationCode(for: Data(message.utf8), using: key)
        return mac.map { String(format: "%02x", $0) }.joined()
    }

    static func randomHex(bytes: Int) -> String {
        var b = [UInt8](repeating: 0, count: bytes)
        _ = SecRandomCopyBytes(kSecRandomDefault, bytes, &b)
        return b.map { String(format: "%02x", $0) }.joined()
    }

    static func constantTimeEqual(_ a: String, _ b: String) -> Bool {
        let x = Array(a.utf8), y = Array(b.utf8)
        guard x.count == y.count else { return false }
        var d: UInt8 = 0
        for i in 0..<x.count { d |= x[i] ^ y[i] }
        return d == 0
    }

    static func newPairingToken() -> String { randomHex(bytes: 24) }   // 48 hex chars
}
