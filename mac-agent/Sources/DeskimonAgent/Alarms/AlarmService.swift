import Foundation
import AppKit
import DeskimonCore

struct StoredAlarm: Codable, Identifiable {
    var id: String
    var fire: Date
    var label: String
}

/// Alarms are persisted, scheduled as native notifications (so they ring even if the Agent is closed) and
/// also tracked by an in-app timer that tells Deskimon to enter the ALERT state.
@MainActor
final class AlarmService {
    private(set) var alarms: [StoredAlarm] = []
    private var timer: Timer?
    private let key = "deskimon.alarms"
    var onFire: ((StoredAlarm) -> Void)?
    private var ringTask: Task<Void, Never>?

    init() {
        if let d = UserDefaults.standard.data(forKey: key), let a = try? JSONDecoder().decode([StoredAlarm].self, from: d) {
            alarms = a.filter { $0.fire > Date().addingTimeInterval(-60) }
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in Task { @MainActor in self?.tick() } }
        for a in alarms { NotificationService.shared.schedule(id: a.id, title: "Deskimon alarm", body: a.label, at: a.fire) }
    }

    func add(at date: Date, label: String) -> StoredAlarm {
        let a = StoredAlarm(id: UUID().uuidString, fire: date, label: label)
        alarms.append(a); persist()
        NotificationService.shared.schedule(id: a.id, title: "Deskimon alarm", body: label, at: date)
        return a
    }

    func cancelAll() { for a in alarms { NotificationService.shared.cancel(id: a.id) }; alarms.removeAll(); persist(); stopRinging() }

    private func persist() { UserDefaults.standard.set(try? JSONEncoder().encode(alarms), forKey: key) }

    private func tick() {
        let now = Date()
        let due = alarms.filter { $0.fire <= now }
        guard !due.isEmpty else { return }
        alarms.removeAll { $0.fire <= now }; persist()
        for a in due { fire(a) }
    }

    private func fire(_ a: StoredAlarm) {
        onFire?(a)
        ringTask?.cancel()
        ringTask = Task { @MainActor in
            for _ in 0..<6 {   // ~18 s of audible alarm on the Mac
                NSSound(named: NSSound.Name("Glass"))?.play()
                try? await Task.sleep(nanoseconds: 3_000_000_000)
                if Task.isCancelled { return }
            }
        }
    }

    func stopRinging() { ringTask?.cancel(); ringTask = nil }
}
