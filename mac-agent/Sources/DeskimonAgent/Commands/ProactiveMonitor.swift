import Foundation
import DeskimonCore

/// Watches the calendar and reminders and pushes heads-ups to the Mac (native notifications) and the device
/// (Concerned face 5 minutes before, Alert when it starts).
@MainActor
final class ProactiveMonitor {
    private let calendar: CalendarService
    private let reminders: ReminderService
    private let hub: DeviceHub
    private var timer: Timer?
    private var warned = Set<String>(), started = Set<String>(), reminded = Set<String>()

    init(calendar: CalendarService, reminders: ReminderService, hub: DeviceHub) { self.calendar = calendar; self.reminders = reminders; self.hub = hub }

    func start() {
        timer = Timer.scheduledTimer(withTimeInterval: 30, repeats: true) { [weak self] _ in Task { @MainActor in await self?.tick() } }
        Task { await tick() }
    }

    func tick() async {
        let now = Date()
        if let items = try? await calendar.list(date: nil, days: 1) {
            for e in items {
                let mins = e.start.timeIntervalSince(now) / 60
                let key = e.id + "\(Int(e.start.timeIntervalSince1970))"
                if mins > 3.5 && mins <= 5.5 && !warned.contains(key) {
                    warned.insert(key)
                    NotificationService.shared.post(title: "Meeting in 5 minutes", body: e.title)
                    hub.sendDeadlineNear(title: "In 5 minutes", subtitle: e.title)
                }
                if mins <= 0.6 && mins > -2 && !started.contains(key) {
                    started.insert(key)
                    NotificationService.shared.post(title: "Meeting starting", body: e.title)
                    hub.sendAlert(kind: "meeting", title: e.title, subtitle: "Starting now - tap to dismiss", holdMs: 0)
                }
            }
        }
        for r in await reminders.incompleteDueSoon(within: 0.02) {
            let key = r.title + "\(Int(r.due.timeIntervalSince1970))"
            if r.due <= now.addingTimeInterval(30), r.due > now.addingTimeInterval(-300), !reminded.contains(key) {
                reminded.insert(key)
                NotificationService.shared.post(title: "Reminder", body: r.title)
                hub.sendAlert(kind: "reminder", title: r.title, subtitle: "Tap to dismiss", holdMs: 0)
            }
        }
        if warned.count > 500 { warned.removeAll(); started.removeAll(); reminded.removeAll() }
    }
}
