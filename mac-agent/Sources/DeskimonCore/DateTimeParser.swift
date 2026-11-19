import Foundation

/// Small deterministic natural-language date/time parser (English). Used by the offline rule parser and
/// as a sanity layer for LLM output. All results are local-time strings: "yyyy-MM-dd" and "HH:mm".
public struct DateTimeParser: Sendable {
    public var now: Date
    public var calendar: Calendar

    public init(now: Date = Date(), calendar: Calendar = .current) {
        self.now = now
        self.calendar = calendar
    }

    static let weekdayNames: [(String, Int)] = [
        ("sunday", 1), ("monday", 2), ("tuesday", 3), ("wednesday", 4), ("thursday", 5), ("friday", 6), ("saturday", 7),
        ("sun", 1), ("mon", 2), ("tue", 3), ("tues", 3), ("wed", 4), ("thu", 5), ("thur", 5), ("thurs", 5), ("fri", 6), ("sat", 7),
    ]
    static let monthNames: [String: Int] = [
        "january": 1, "february": 2, "march": 3, "april": 4, "may": 5, "june": 6, "july": 7, "august": 8,
        "september": 9, "october": 10, "november": 11, "december": 12,
        "jan": 1, "feb": 2, "mar": 3, "apr": 4, "jun": 6, "jul": 7, "aug": 8, "sep": 9, "sept": 9, "oct": 10, "nov": 11, "dec": 12,
    ]

    public func dateString(_ d: Date) -> String {
        let c = calendar.dateComponents([.year, .month, .day], from: d)
        return String(format: "%04d-%02d-%02d", c.year ?? 1970, c.month ?? 1, c.day ?? 1)
    }

    public func parseDateString(_ s: String) -> Date? {
        let p = s.split(separator: "-").compactMap { Int($0) }
        guard p.count == 3 else { return nil }
        var c = DateComponents(); c.year = p[0]; c.month = p[1]; c.day = p[2]; c.hour = 12
        guard let d = calendar.date(from: c) else { return nil }
        // reject overflow like 2025-02-31
        let back = calendar.dateComponents([.year, .month, .day], from: d)
        return (back.year == p[0] && back.month == p[1] && back.day == p[2]) ? d : nil
    }

    public static func isValidTime(_ s: String) -> Bool {
        let p = s.split(separator: ":").compactMap { Int($0) }
        return p.count == 2 && (0...23).contains(p[0]) && (0...59).contains(p[1])
    }

    /// Combine "yyyy-MM-dd" + "HH:mm" into a Date in `calendar`'s time zone.
    public func combine(date: String, time: String) -> Date? {
        guard let d = parseDateString(date), Self.isValidTime(time) else { return nil }
        let t = time.split(separator: ":").compactMap { Int($0) }
        var c = calendar.dateComponents([.year, .month, .day], from: d)
        c.hour = t[0]; c.minute = t[1]; c.second = 0
        return calendar.date(from: c)
    }

    /// Parse a date phrase out of `text`. Returns the date string and the text with the phrase removed.
    public func extractDate(from text: String) -> (date: String, rest: String, weekday: Int?)? {
        let lower = text.lowercased()
        func strip(_ pattern: String) -> String {
            lower.replacingOccurrences(of: pattern, with: " ", options: [.regularExpression, .caseInsensitive])
        }
        let today = calendar.startOfDay(for: now)

        if lower.range(of: #"\bday after tomorrow\b"#, options: .regularExpression) != nil {
            return (dateString(calendar.date(byAdding: .day, value: 2, to: today)!), strip(#"\bday after tomorrow\b"#), nil)
        }
        if lower.range(of: #"\b(tomorrow|tmrw|tmr)\b"#, options: .regularExpression) != nil {
            return (dateString(calendar.date(byAdding: .day, value: 1, to: today)!), strip(#"\b(tomorrow|tmrw|tmr)\b"#), nil)
        }
        if lower.range(of: #"\b(today|tonight|this evening|this afternoon|this morning)\b"#, options: .regularExpression) != nil {
            return (dateString(today), strip(#"\b(today|tonight|this evening|this afternoon|this morning)\b"#), nil)
        }
        // "in 3 days", "in 2 weeks"
        if let r = lower.range(of: #"\bin (\d+) (day|days|week|weeks)\b"#, options: .regularExpression) {
            let m = String(lower[r]).split(separator: " ")
            if m.count == 3, let n = Int(m[1]) {
                let days = m[2].hasPrefix("week") ? n * 7 : n
                return (dateString(calendar.date(byAdding: .day, value: days, to: today)!), strip(#"\bin \d+ (day|days|week|weeks)\b"#), nil)
            }
        }
        // weekday names ("friday", "next monday", "on tue")
        for (name, wd) in Self.weekdayNames.sorted(by: { $0.0.count > $1.0.count }) {
            let pat = #"\b(?:(next|this|on)\s+)?"# + name + #"\b"#
            if let r = lower.range(of: pat, options: .regularExpression) {
                let phrase = String(lower[r])
                let cur = calendar.component(.weekday, from: today)
                var delta = (wd - cur + 7) % 7
                if delta == 0 && phrase.hasPrefix("next") { delta = 7 }
                return (dateString(calendar.date(byAdding: .day, value: delta, to: today)!), strip(pat), wd)
            }
        }
        // "12 march", "march 12", "12th of march"
        let months = Self.monthNames.keys.sorted { $0.count > $1.count }.joined(separator: "|")
        if let r = lower.range(of: #"\b(\d{1,2})(?:st|nd|rd|th)?\s+(?:of\s+)?("# + months + #")\b"#, options: .regularExpression) {
            let parts = String(lower[r]).replacingOccurrences(of: "of ", with: "").split(separator: " ")
            let digits = parts[0].filter(\.isNumber)
            if let d = Int(digits), let mo = Self.monthNames[String(parts[1])] { return (resolve(month: mo, day: d, today: today), strip(#"\b(\d{1,2})(?:st|nd|rd|th)?\s+(?:of\s+)?("# + months + #")\b"#), nil) }
        }
        if let r = lower.range(of: #"\b("# + months + #")\s+(\d{1,2})(?:st|nd|rd|th)?\b"#, options: .regularExpression) {
            let parts = String(lower[r]).split(separator: " ")
            let digits = parts[1].filter(\.isNumber)
            if let d = Int(digits), let mo = Self.monthNames[String(parts[0])] { return (resolve(month: mo, day: d, today: today), strip(#"\b("# + months + #")\s+(\d{1,2})(?:st|nd|rd|th)?\b"#), nil) }
        }
        // ISO date
        if let r = lower.range(of: #"\b\d{4}-\d{2}-\d{2}\b"#, options: .regularExpression) {
            let s = String(lower[r]); if parseDateString(s) != nil { return (s, strip(#"\b\d{4}-\d{2}-\d{2}\b"#), nil) }
        }
        return nil
    }

    private func resolve(month: Int, day: Int, today: Date) -> String {
        let y = calendar.component(.year, from: today)
        var c = DateComponents(); c.year = y; c.month = month; c.day = day; c.hour = 12
        if let d = calendar.date(from: c), d >= today { return dateString(d) }
        c.year = y + 1
        return dateString(calendar.date(from: c) ?? today)
    }

    /// Extract the first time-of-day in `text` ("4 pm", "4:30pm", "16:00", "at 4", "noon").
    /// Bare hours without am/pm: 1-6 -> PM, 7-11 -> AM, 12 -> PM (typical work-day reading).
    public func extractTime(from text: String) -> (time: String, rest: String)? {
        let lower = text.lowercased()
        func strip(_ r: Range<String.Index>) -> String { var s = lower; s.replaceSubrange(r, with: " "); return s }
        if let r = lower.range(of: #"\bnoon\b"#, options: .regularExpression) { return ("12:00", strip(r)) }
        if let r = lower.range(of: #"\bmidnight\b"#, options: .regularExpression) { return ("00:00", strip(r)) }
        if let r = lower.range(of: #"\b(\d{1,2})(?::(\d{2}))?\s*(a\.?m\.?|p\.?m\.?)(?=\W|$)"#, options: .regularExpression) {
            let s = String(lower[r]).replacingOccurrences(of: ".", with: "")
            let isPM = s.contains("pm")
            let nums = s.replacingOccurrences(of: "am", with: "").replacingOccurrences(of: "pm", with: "").trimmingCharacters(in: .whitespaces)
            let hm = nums.split(separator: ":").compactMap { Int($0) }
            guard var h = hm.first, (1...12).contains(h) else { return nil }
            let m = hm.count > 1 ? hm[1] : 0
            guard (0...59).contains(m) else { return nil }
            if h == 12 { h = isPM ? 12 : 0 } else if isPM { h += 12 }
            return (String(format: "%02d:%02d", h, m), strip(r))
        }
        if let r = lower.range(of: #"\b([01]?\d|2[0-3]):([0-5]\d)\b"#, options: .regularExpression) {
            let hm = String(lower[r]).split(separator: ":").compactMap { Int($0) }
            return (String(format: "%02d:%02d", hm[0], hm[1]), strip(r))
        }
        if let r = lower.range(of: #"\b(?:at|by|around)\s+(\d{1,2})\b(?!\s*(?:min|minute|minutes|hour|hours|people|items))"#, options: .regularExpression) {
            let digits = String(lower[r]).filter(\.isNumber)
            if var h = Int(digits), (1...12).contains(h) {
                if h <= 6 || h == 12 { h = (h == 12) ? 12 : h + 12 }
                return (String(format: "%02d:00", h), strip(r))
            }
        }
        return nil
    }

    /// "for 30 minutes", "for 2 hours", "1 hour"
    public func extractDuration(from text: String) -> Int? {
        let lower = text.lowercased()
        if let r = lower.range(of: #"\b(?:for\s+)?(\d+(?:\.\d+)?)\s*(hours?|hrs?|minutes?|mins?)\b"#, options: .regularExpression) {
            let s = String(lower[r])
            let num = s.split(whereSeparator: { !($0.isNumber || $0 == ".") }).first.flatMap { Double($0) }
            if let n = num { return s.contains("h") ? Int(n * 60) : Int(n) }
        }
        return nil
    }
}
