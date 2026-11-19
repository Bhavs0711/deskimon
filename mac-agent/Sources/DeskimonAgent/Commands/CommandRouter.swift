import Foundation
import EventKit
import DeskimonCore

/// THE single command execution layer. Every input path (Deskimon touch/voice over Wi-Fi or BLE, Mac text box,
/// Mac dictation, scheduled triggers) ends up in `execute`. Intents are validated again here, sensitive ones
/// require a Mac-side confirmation, and there is deliberately no way to run arbitrary shell commands.
@MainActor
final class CommandRouter {
    let calendar: CalendarService
    let reminders: ReminderService
    let alarms: AlarmService
    let email: EmailService
    let settings: AppSettings
    var launcher: AppLauncher { AppLauncher(policy: FilePolicy(projectFolder: settings.projectFolder)) }
    private var dt: DateTimeParser { DateTimeParser() }

    init(calendar: CalendarService, reminders: ReminderService, alarms: AlarmService, email: EmailService, settings: AppSettings) {
        self.calendar = calendar; self.reminders = reminders; self.alarms = alarms; self.email = email; self.settings = settings
    }

    func execute(_ i: Intent, origin: Origin) async -> CommandResult {
        do { try IntentValidator(dt: dt).validate(i) }
        catch { return CommandResult(success: false, message: error.localizedDescription, emotion: "curious", intent: i.intent.rawValue, title: "Need more info", overlay: "status") }
        let name = i.intent.rawValue
        do {
            switch i.intent {
            case .createEvent:
                let item = try await calendar.create(title: i.title ?? "Meeting", date: i.date!, time: i.time!, endTime: i.endTime, minutes: i.durationMinutes,
                                                     defaultMinutes: settings.defaultEventMinutes, recurrence: i.recurrence, weekday: i.weekday, location: i.location, notes: i.notes)
                let when = Self.friendly(item.start)
                return CommandResult(success: true, message: "\(item.title) scheduled \(when).", emotion: "success", intent: name, title: "Event added", overlay: "meeting", detail: [item.title, when])
            case .updateEvent:
                let item = try await calendar.move(query: i.query ?? i.title ?? "", date: i.date, newDate: i.newDate, newTime: i.newTime)
                return CommandResult(success: true, message: "Moved \(item.title) to \(Self.friendly(item.start)).", emotion: "happy", intent: name, title: "Event moved", overlay: "meeting")
            case .deleteEvent:
                let (ev, desc) = try await calendar.describeForDelete(query: i.query ?? i.title ?? "", date: i.date)
                let ok = await ConfirmationGate.confirm(title: "Delete this calendar event?", detail: desc + (origin == .deskimon ? "\n\nRequested from Deskimon." : ""), confirmButton: "Delete", destructive: true)
                guard ok else { return CommandResult(success: false, message: "Okay, I left it on your calendar.", emotion: "calm", intent: name, title: "Cancelled", overlay: "info") }
                try calendar.delete(ev)
                return CommandResult(success: true, message: "Deleted \(ev.title ?? "the event").", emotion: "success", intent: name, title: "Event deleted", overlay: "meeting")
            case .listEvents, .getSchedule:
                return try await listResult(i)
            case .createReminder:
                let due = try await reminders.create(title: i.title ?? "Reminder", date: i.date, time: i.time, recurrence: i.recurrence, weekday: i.weekday, notes: i.notes)
                var m = "I'll remind you to \(Self.lowerFirst(i.title ?? "do that"))"
                if let due { m += " " + Self.friendly(due) }
                if let r = i.recurrence { m += " (\(r.rawValue))" }
                return CommandResult(success: true, message: m + ".", emotion: "success", intent: name, title: "Reminder set", overlay: "reminder")
            case .createAlarm:
                guard let when = dt.combine(date: i.date ?? dt.dateString(dt.now), time: i.time!), when > Date() else {
                    return CommandResult(success: false, message: "That time has already passed.", emotion: "confused", intent: name, title: "Alarm", overlay: "status")
                }
                _ = alarms.add(at: when, label: i.title ?? "Alarm")
                return CommandResult(success: true, message: "Alarm set \(Self.friendly(when)).", emotion: "success", intent: name, title: "Alarm set", overlay: "alarm")
            case .draftEmail:
                let d = await email.createDraft(from: i)
                let who = d.to.isEmpty ? "(add recipient)" : d.to
                return CommandResult(success: true, message: "Your draft is ready on your Mac. Review it and approve to send.", emotion: "focused", intent: name,
                                     title: "Email draft", overlay: "email", needsConfirmation: true, detail: [who, d.subject])
            case .sendEmail, .confirm:
                guard let d = email.pending else { return CommandResult(success: false, message: "There's no email draft waiting.", emotion: "confused", intent: name, title: "No draft", overlay: "status") }
                guard !d.to.isEmpty else { return CommandResult(success: false, message: "The draft needs a recipient. Please add one in the Agent window.", emotion: "concerned", intent: name, title: "Email draft", overlay: "email", needsConfirmation: true) }
                let ok = await ConfirmationGate.confirm(title: "Send this email?", detail: "To: \(d.to)\nSubject: \(d.subject)\n\n\(d.body.prefix(600))", confirmButton: "Send")
                guard ok else { return CommandResult(success: false, message: "Okay, I won't send it. The draft is still here.", emotion: "calm", intent: name, title: "Not sent", overlay: "email") }
                try await email.send(d)
                return CommandResult(success: true, message: "Email sent to \(d.to).", emotion: "proud", intent: name, title: "Email sent", overlay: "email")
            case .cancel:
                if email.pending != nil { email.discard(); return CommandResult(success: true, message: "Draft discarded.", emotion: "calm", intent: name, title: "Discarded", overlay: "email") }
                alarms.stopRinging()
                return CommandResult(success: true, message: "Okay.", emotion: "calm", intent: name, title: "Okay", overlay: "info")
            case .openApp: return await launcher.openApp(i.target ?? "")
            case .openURL: return launcher.openURL(i.target ?? "")
            case .openFile: return launcher.openFile(i.target ?? "")
            case .generalWorkQuery:
                let reply = (i.reply?.isEmpty == false) ? i.reply! : "I'm best at work stuff: meetings, reminders, emails and planning. What should we get done?"
                return CommandResult(success: true, message: reply, emotion: DeskimonEmotion.sanitize(i.emotion, fallback: "happy"), intent: name, title: "Deskimon", overlay: "info")
            }
        } catch {
            return CommandResult(success: false, message: error.localizedDescription, emotion: "error", intent: name, title: "Couldn't do that", overlay: "status")
        }
    }

    private func listResult(_ i: Intent) async throws -> CommandResult {
        let name = i.intent.rawValue
        if i.query == "__next__" {
            guard let n = try await calendar.next() else { return CommandResult(success: true, message: "You have nothing coming up in the next two weeks.", emotion: "calm", intent: name, title: "Next meeting", overlay: "meeting") }
            return CommandResult(success: true, message: "Your next event is \(n.title) \(Self.friendly(n.start)).", emotion: "happy", intent: name, title: "Next up", overlay: "meeting", detail: [n.title, Self.friendly(n.start)])
        }
        let days = i.rangeDays ?? 1
        let items = try await calendar.list(date: i.date, days: days)
        let dayWord = days == 1 ? (i.date == dt.dateString(dt.now) || i.date == nil ? "today" : "that day") : "in the next \(days) days"
        if items.isEmpty { return CommandResult(success: true, message: "Nothing on your calendar \(dayWord). A good time to focus.", emotion: "calm", intent: name, title: "Schedule", overlay: "meeting") }
        let f = DateFormatter(); f.dateFormat = days > 1 ? "EEE h:mm a" : "h:mm a"
        let lines = items.prefix(5).map { "\(f.string(from: $0.start)) \($0.title)" }
        let spoken = items.prefix(3).map { "\($0.title) at \(f.string(from: $0.start))" }.joined(separator: ", ")
        let more = items.count > 3 ? " and \(items.count - 3) more" : ""
        return CommandResult(success: true, message: "You have \(items.count) \(items.count == 1 ? "event" : "events") \(dayWord): \(spoken)\(more).", emotion: items.count >= 5 ? "busy" : "happy",
                             intent: name, title: "\(items.count) \(items.count == 1 ? "event" : "events")", overlay: "meeting", detail: Array(lines))
    }

    // MARK: formatting
    static func friendly(_ d: Date, now: Date = Date()) -> String {
        let cal = Calendar.current
        let t = DateFormatter(); t.dateFormat = "h:mm a"
        let time = t.string(from: d)
        if cal.isDate(d, inSameDayAs: now) { return "today at \(time)" }
        if let tm = cal.date(byAdding: .day, value: 1, to: now), cal.isDate(d, inSameDayAs: tm) { return "tomorrow at \(time)" }
        let f = DateFormatter(); f.dateFormat = "EEEE, MMM d"
        return "\(f.string(from: d)) at \(time)"
    }
    static func lowerFirst(_ s: String) -> String { s.prefix(1).lowercased() + s.dropFirst() }
}
