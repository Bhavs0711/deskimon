import Foundation
import EventKit
import DeskimonCore

struct CalendarItem {
    let id: String, title: String, start: Date, end: Date
}

enum CalendarError: LocalizedError {
    case denied, notFound(String), ambiguous([String]), invalid(String)
    var errorDescription: String? {
        switch self {
        case .denied: return "Calendar access is off. Allow it in System Settings > Privacy & Security > Calendars."
        case .notFound(let q): return "I couldn't find an event matching \"\(q)\"."
        case .ambiguous(let t): return "That matches several events: \(t.joined(separator: ", ")). Which one?"
        case .invalid(let m): return m
        }
    }
}

/// Real macOS calendar integration through EventKit. Requests authorization via the system prompt.
final class CalendarService {
    private let store = EKEventStore()
    let dt: () -> DateTimeParser
    init(dt: @escaping () -> DateTimeParser = { DateTimeParser() }) { self.dt = dt }

    func requestAccess() async -> Bool {
        do { return try await store.requestFullAccessToEvents() } catch { return false }
    }

    private func ensureAccess() async throws {
        switch EKEventStore.authorizationStatus(for: .event) {
        case .fullAccess: return
        case .notDetermined: if await requestAccess() { return }; throw CalendarError.denied
        default: throw CalendarError.denied
        }
    }

    // MARK: create
    func create(title: String, date: String, time: String, endTime: String?, minutes: Int?, defaultMinutes: Int,
                recurrence: Recurrence?, weekday: Int?, location: String?, notes: String?) async throws -> CalendarItem {
        try await ensureAccess()
        let p = dt()
        guard let start = p.combine(date: date, time: time) else { throw CalendarError.invalid("That date or time doesn't look right.") }
        var end = start.addingTimeInterval(TimeInterval((minutes ?? defaultMinutes) * 60))
        if let endTime, let e = p.combine(date: date, time: endTime), e > start { end = e }
        let ev = EKEvent(eventStore: store)
        ev.title = title; ev.startDate = start; ev.endDate = end
        ev.location = location; ev.notes = notes
        guard let cal = store.defaultCalendarForNewEvents else { throw CalendarError.invalid("No default calendar is configured.") }
        ev.calendar = cal
        if let rule = Self.rule(for: recurrence, weekday: weekday) { ev.addRecurrenceRule(rule) }
        try store.save(ev, span: .thisEvent, commit: true)
        return CalendarItem(id: ev.eventIdentifier, title: title, start: start, end: end)
    }

    static func rule(for r: Recurrence?, weekday: Int?) -> EKRecurrenceRule? {
        guard let r else { return nil }
        switch r {
        case .daily: return EKRecurrenceRule(recurrenceWith: .daily, interval: 1, end: nil)
        case .weekly:
            if let weekday, let d = EKWeekday(rawValue: weekday) {
                return EKRecurrenceRule(recurrenceWith: .weekly, interval: 1, daysOfTheWeek: [EKRecurrenceDayOfWeek(d)],
                                        daysOfTheMonth: nil, monthsOfTheYear: nil, weeksOfTheYear: nil, daysOfTheYear: nil, setPositions: nil, end: nil)
            }
            return EKRecurrenceRule(recurrenceWith: .weekly, interval: 1, end: nil)
        case .weekdays:
            let days = [2, 3, 4, 5, 6].compactMap { EKWeekday(rawValue: $0) }.map { EKRecurrenceDayOfWeek($0) }
            return EKRecurrenceRule(recurrenceWith: .weekly, interval: 1, daysOfTheWeek: days, daysOfTheMonth: nil,
                                    monthsOfTheYear: nil, weeksOfTheYear: nil, daysOfTheYear: nil, setPositions: nil, end: nil)
        case .monthly: return EKRecurrenceRule(recurrenceWith: .monthly, interval: 1, end: nil)
        }
    }

    // MARK: list / search
    func events(from start: Date, to end: Date) async throws -> [EKEvent] {
        try await ensureAccess()
        let pred = store.predicateForEvents(withStart: start, end: end, calendars: nil)
        return store.events(matching: pred).sorted { $0.startDate < $1.startDate }
    }

    func list(date: String?, days: Int) async throws -> [CalendarItem] {
        let p = dt()
        let day = date.flatMap { p.parseDateString($0) } ?? p.now
        let start = p.calendar.startOfDay(for: day)
        let end = p.calendar.date(byAdding: .day, value: max(days, 1), to: start) ?? start.addingTimeInterval(86400)
        return try await events(from: start, to: end).map { CalendarItem(id: $0.eventIdentifier, title: $0.title ?? "Untitled", start: $0.startDate, end: $0.endDate) }
    }

    func next() async throws -> CalendarItem? {
        let p = dt()
        let evs = try await events(from: p.now, to: p.now.addingTimeInterval(14 * 86400))
        return evs.first.map { CalendarItem(id: $0.eventIdentifier, title: $0.title ?? "Untitled", start: $0.startDate, end: $0.endDate) }
    }

    /// Find a single event by fuzzy title (all words must match) in a window.
    func find(query: String, date: String?) async throws -> EKEvent {
        let p = dt()
        let start: Date, end: Date
        if let date, let d = p.parseDateString(date) {
            start = p.calendar.startOfDay(for: d); end = p.calendar.date(byAdding: .day, value: 1, to: start)!
        } else {
            start = p.calendar.startOfDay(for: p.now); end = p.calendar.date(byAdding: .day, value: 45, to: start)!
        }
        let all = try await events(from: start, to: end)
        let words = query.lowercased().split(separator: " ").map(String.init).filter { $0.count > 1 }
        let matches = all.filter { ev in
            let t = (ev.title ?? "").lowercased()
            return words.isEmpty || words.allSatisfy { t.contains($0) }
        }
        if matches.isEmpty { throw CalendarError.notFound(query) }
        if matches.count > 1 && date == nil && !words.isEmpty {
            // prefer the soonest upcoming one when the title is the same series
            let titles = Set(matches.map { $0.title ?? "" })
            if titles.count > 1 { throw CalendarError.ambiguous(Array(titles.prefix(3))) }
        }
        if matches.count > 1 && words.isEmpty { throw CalendarError.ambiguous(matches.prefix(3).map { $0.title ?? "Untitled" }) }
        return matches.first(where: { $0.startDate >= p.now }) ?? matches[0]
    }

    // MARK: update / delete
    func move(query: String, date: String?, newDate: String?, newTime: String?) async throws -> CalendarItem {
        let ev = try await find(query: query, date: date)
        let p = dt()
        let cal = p.calendar
        let dur = ev.endDate.timeIntervalSince(ev.startDate)
        let oldDay = p.dateString(ev.startDate)
        let oldTime = String(format: "%02d:%02d", cal.component(.hour, from: ev.startDate), cal.component(.minute, from: ev.startDate))
        guard let ns = p.combine(date: newDate ?? oldDay, time: newTime ?? oldTime) else { throw CalendarError.invalid("That new time doesn't look right.") }
        ev.startDate = ns; ev.endDate = ns.addingTimeInterval(dur)
        try store.save(ev, span: .thisEvent, commit: true)
        return CalendarItem(id: ev.eventIdentifier, title: ev.title ?? "Untitled", start: ns, end: ev.endDate)
    }

    func describeForDelete(query: String, date: String?) async throws -> (EKEvent, String) {
        let ev = try await find(query: query, date: date)
        let f = DateFormatter(); f.dateStyle = .medium; f.timeStyle = .short
        return (ev, "\(ev.title ?? "Untitled") - \(f.string(from: ev.startDate))")
    }

    func delete(_ ev: EKEvent) throws { try store.remove(ev, span: .thisEvent, commit: true) }
}
