import Foundation

/// Every action Deskimon can perform. The LLM / rule parser only ever *proposes* one of these;
/// the CommandRouter validates and executes it.
public enum IntentName: String, Codable, CaseIterable, Sendable {
    case createEvent = "create_event"
    case updateEvent = "update_event"
    case deleteEvent = "delete_event"
    case listEvents = "list_events"
    case createReminder = "create_reminder"
    case createAlarm = "create_alarm"
    case draftEmail = "draft_email"
    case sendEmail = "send_email"
    case openApp = "open_app"
    case openFile = "open_file"
    case openURL = "open_url"
    case getSchedule = "get_schedule"
    case generalWorkQuery = "general_work_query"
    case confirm = "confirm"        // "yes, send it"
    case cancel = "cancel"          // "no / cancel"
}

public enum Recurrence: String, Codable, Sendable {
    case daily, weekdays, weekly, monthly
}

/// Structured parameters extracted from natural language.
public struct Intent: Codable, Equatable, Sendable {
    public var intent: IntentName
    public var title: String?
    public var date: String?            // yyyy-MM-dd (local)
    public var time: String?            // HH:mm (24h, local)
    public var endTime: String?         // HH:mm
    public var durationMinutes: Int?
    public var newDate: String?         // update_event
    public var newTime: String?         // update_event
    public var query: String?           // search / which event
    public var rangeDays: Int?          // list_events / get_schedule: how many days from `date` (default 1)
    public var recurrence: Recurrence?
    public var weekday: Int?            // 1=Sunday ... 7=Saturday (for weekly recurrence)
    public var recipient: String?
    public var subject: String?
    public var body: String?
    public var target: String?          // app name / url / path
    public var reply: String?           // general_work_query answer or spoken text
    public var emotion: String?         // suggested face emotion
    public var location: String?
    public var notes: String?
    public var sourceText: String?      // original utterance

    public init(intent: IntentName) { self.intent = intent }

    enum CodingKeys: String, CodingKey {
        case intent, title, date, time
        case endTime = "end_time", durationMinutes = "duration_minutes"
        case newDate = "new_date", newTime = "new_time"
        case query, rangeDays = "range_days", recurrence, weekday
        case recipient, subject, body, target, reply, emotion, location, notes
        case sourceText = "source_text"
    }
}

public enum Origin: String, Codable, Sendable {
    case deskimon   // physical device (touch / voice / BLE / Wi-Fi)
    case mac        // Mac Agent UI / voice
    case scheduler  // internal timers
}

public struct CommandResult: Codable, Equatable, Sendable {
    public var success: Bool
    public var message: String
    public var emotion: String
    public var intent: String?
    public var title: String?           // short overlay title
    public var overlay: String?         // meeting | reminder | alarm | email | info | status
    public var needsConfirmation: Bool
    public var detail: [String]?        // extra lines (e.g. a list of events)

    public init(success: Bool, message: String, emotion: String, intent: String? = nil,
                title: String? = nil, overlay: String? = nil, needsConfirmation: Bool = false, detail: [String]? = nil) {
        self.success = success; self.message = message; self.emotion = emotion; self.intent = intent
        self.title = title; self.overlay = overlay; self.needsConfirmation = needsConfirmation; self.detail = detail
    }

    public static func failure(_ msg: String, intent: String? = nil) -> CommandResult {
        CommandResult(success: false, message: msg, emotion: "error", intent: intent, title: "Couldn't do that", overlay: "status")
    }
}
