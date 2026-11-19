import Foundation
import EventKit
import DeskimonCore

/// Apple Reminders through EventKit (due dates, alarms and recurring reminders).
final class ReminderService {
    private let store = EKEventStore()
    let dt: () -> DateTimeParser
    init(dt: @escaping () -> DateTimeParser = { DateTimeParser() }) { self.dt = dt }

    func requestAccess() async -> Bool { (try? await store.requestFullAccessToReminders()) ?? false }

    private func ensureAccess() async throws {
        switch EKEventStore.authorizationStatus(for: .reminder) {
        case .fullAccess: return
        case .notDetermined: if await requestAccess() { return }; throw CalendarError.invalid("Reminders access was not granted.")
        default: throw CalendarError.invalid("Reminders access is off. Allow it in System Settings > Privacy & Security > Reminders.")
        }
    }

    @discardableResult
    func create(title: String, date: String?, time: String?, recurrence: Recurrence?, weekday: Int?, notes: String?) async throws -> Date? {
        try await ensureAccess()
        let p = dt()
        let r = EKReminder(eventStore: store)
        r.title = title; r.notes = notes
        guard let cal = store.defaultCalendarForNewReminders() else { throw CalendarError.invalid("No default reminders list is configured.") }
        r.calendar = cal
        var due: Date?
        if let time {
            var day = date
            if day == nil { // next occurrence of that time (or next matching weekday for weekly)
                var d = p.dateString(p.now)
                if let weekday, recurrence == .weekly {
                    let cur = p.calendar.component(.weekday, from: p.now)
                    d = p.dateString(p.calendar.date(byAdding: .day, value: (weekday - cur + 7) % 7, to: p.now)!)
                }
                day = d
            }
            if let day, var dd = p.combine(date: day, time: time) {
                if dd <= p.now && recurrence == nil && date == nil { dd = p.calendar.date(byAdding: .day, value: 1, to: dd)! }
                due = dd
            }
        } else if let date, let d = p.parseDateString(date) {
            due = p.calendar.date(bySettingHour: 9, minute: 0, second: 0, of: d)
        }
        if let due {
            r.dueDateComponents = p.calendar.dateComponents([.year, .month, .day, .hour, .minute], from: due)
            r.addAlarm(EKAlarm(absoluteDate: due))
        }
        if let rule = CalendarService.rule(for: recurrence, weekday: weekday) { r.addRecurrenceRule(rule) }
        try store.save(r, commit: true)
        return due
    }

    func incompleteDueSoon(within hours: Double) async -> [(title: String, due: Date)] {
        guard (try? await ensureAccess()) != nil else { return [] }
        let pred = store.predicateForIncompleteReminders(withDueDateStarting: nil, ending: Date().addingTimeInterval(hours * 3600), calendars: nil)
        return await withCheckedContinuation { cont in
            store.fetchReminders(matching: pred) { rs in
                let out = (rs ?? []).compactMap { r -> (String, Date)? in
                    guard let c = r.dueDateComponents, let d = Calendar.current.date(from: c) else { return nil }
                    return (r.title ?? "Reminder", d)
                }.sorted { $0.1 < $1.1 }
                cont.resume(returning: out.map { (title: $0.0, due: $0.1) })
            }
        }
    }
}
