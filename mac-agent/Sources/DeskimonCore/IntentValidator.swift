import Foundation

/// Hard validation of any Intent before execution (whatever produced it: LLM, rules or the network).
/// The LLM never executes anything; this is the gate in front of the CommandRouter.
public enum IntentValidationError: Error, Equatable, LocalizedError {
    case invalid(String)
    public var errorDescription: String? { if case .invalid(let m) = self { return m } else { return nil } }
}

public struct IntentValidator: Sendable {
    public var dt: DateTimeParser
    public init(dt: DateTimeParser = DateTimeParser()) { self.dt = dt }

    public static let maxText = 4000

    public func validate(_ i: Intent) throws {
        func check(_ s: String?, _ name: String, max: Int = 400) throws {
            if let s, s.count > max { throw IntentValidationError.invalid("\(name) is too long") }
            if let s, s.unicodeScalars.contains(where: { $0.value < 0x20 && $0 != "\n" && $0 != "\t" }) { throw IntentValidationError.invalid("\(name) contains control characters") }
        }
        try check(i.title, "title"); try check(i.query, "query"); try check(i.recipient, "recipient", max: 320)
        try check(i.subject, "subject", max: 300); try check(i.body, "body", max: Self.maxText)
        try check(i.target, "target", max: 1024); try check(i.reply, "reply", max: Self.maxText); try check(i.location, "location")

        if let d = i.date, dt.parseDateString(d) == nil { throw IntentValidationError.invalid("bad date '\(d)'") }
        if let d = i.newDate, dt.parseDateString(d) == nil { throw IntentValidationError.invalid("bad new date '\(d)'") }
        for t in [i.time, i.endTime, i.newTime].compactMap({ $0 }) where !DateTimeParser.isValidTime(t) { throw IntentValidationError.invalid("bad time '\(t)'") }
        if let m = i.durationMinutes, !(1...(24 * 60)).contains(m) { throw IntentValidationError.invalid("bad duration") }
        if let r = i.rangeDays, !(1...90).contains(r) { throw IntentValidationError.invalid("bad range") }
        if let w = i.weekday, !(1...7).contains(w) { throw IntentValidationError.invalid("bad weekday") }

        switch i.intent {
        case .createEvent:
            guard i.title?.isEmpty == false else { throw IntentValidationError.invalid("What should I call the event?") }
            guard i.date != nil, i.time != nil else { throw IntentValidationError.invalid("What day and time should I schedule it?") }
        case .updateEvent:
            guard i.query?.isEmpty == false || i.title?.isEmpty == false else { throw IntentValidationError.invalid("Which event should I move?") }
            guard i.newDate != nil || i.newTime != nil else { throw IntentValidationError.invalid("When should I move it to?") }
        case .deleteEvent:
            guard i.query?.isEmpty == false || i.title?.isEmpty == false else { throw IntentValidationError.invalid("Which event should I cancel?") }
        case .createReminder:
            guard i.title?.isEmpty == false else { throw IntentValidationError.invalid("What should I remind you about?") }
        case .createAlarm:
            guard i.time != nil else { throw IntentValidationError.invalid("What time should the alarm ring?") }
        case .draftEmail:
            guard i.recipient?.isEmpty == false || i.body?.isEmpty == false else { throw IntentValidationError.invalid("Who is the email for?") }
        case .openApp, .openFile, .openURL:
            guard i.target?.isEmpty == false else { throw IntentValidationError.invalid("What should I open?") }
        default: break
        }
    }

    /// Basic RFC-ish email check (no display names).
    public static func isEmail(_ s: String) -> Bool {
        s.range(of: #"^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$"#, options: .regularExpression) != nil
    }
}
