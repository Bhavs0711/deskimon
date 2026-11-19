import Foundation

/// Wire protocol shared with the ESP32 firmware: newline-delimited JSON objects (docs/PROTOCOL.md).
public enum WireProtocol {
    public static let tcpPort: UInt16 = 8765
    public static let discoveryPort: UInt16 = 8766
    public static let bleServiceUUID = "6E5D0001-B5A3-F393-E0A9-E50E24DCCA9E"
    public static let bleRxUUID      = "6E5D0002-B5A3-F393-E0A9-E50E24DCCA9E"   // Mac -> device (write)
    public static let bleTxUUID      = "6E5D0003-B5A3-F393-E0A9-E50E24DCCA9E"   // device -> Mac (notify)
    public static let bleCfgUUID     = "6E5D0004-B5A3-F393-E0A9-E50E24DCCA9E"   // provisioning (write)

    public typealias JSON = [String: Any]

    public static func encode(_ obj: JSON) -> Data? {
        guard JSONSerialization.isValidJSONObject(obj),
              var d = try? JSONSerialization.data(withJSONObject: obj, options: [.sortedKeys]) else { return nil }
        d.append(0x0A)
        return d
    }

    public static func decode(_ line: Data) -> JSON? {
        (try? JSONSerialization.jsonObject(with: line)) as? JSON
    }

    /// Split a byte buffer into complete lines, leaving any trailing partial line in `buffer`.
    public static func drainLines(_ buffer: inout Data, maxLine: Int = 64 * 1024) -> [Data] {
        var lines: [Data] = []
        while let idx = buffer.firstIndex(of: 0x0A) {
            let line = buffer[buffer.startIndex..<idx]
            if !line.isEmpty { lines.append(Data(line)) }
            buffer.removeSubrange(buffer.startIndex...idx)
        }
        if buffer.count > maxLine { buffer.removeAll() }
        return lines
    }

    public static func resultMessage(id: Int?, _ r: CommandResult) -> JSON {
        var m: JSON = ["type": "result", "success": r.success, "message": r.message, "emotion": r.emotion]
        if let id { m["id"] = id }
        if let i = r.intent { m["intent"] = i }
        if let t = r.title { m["title"] = t }
        if let o = r.overlay { m["overlay"] = o }
        if r.needsConfirmation { m["needs_confirmation"] = true }
        return m
    }
}

/// Emotions understood by the firmware (dsk_emotion.h). Validated before sending.
public enum DeskimonEmotion {
    public static let all: Set<String> = [
        "neutral", "happy", "very_happy", "excited", "focused", "thinking", "listening", "speaking", "processing",
        "confused", "concerned", "worried", "tired", "sleepy", "sleeping", "surprised", "proud", "celebrating",
        "curious", "waiting", "alert", "deadline_warning", "success", "error", "disappointed", "motivating",
        "busy", "calm", "disconnected", "pairing",
    ]
    public static func sanitize(_ s: String?, fallback: String = "happy") -> String {
        guard let s = s?.lowercased(), all.contains(s) else { return fallback }
        return s
    }
}
