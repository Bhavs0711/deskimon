import Foundation
import UserNotifications

/// Native macOS notifications. Requires running from the .app bundle (scripts/build_mac_agent.sh).
final class NotificationService: NSObject, UNUserNotificationCenterDelegate {
    static let shared = NotificationService()
    private var available: Bool { Bundle.main.bundleIdentifier != nil }

    func requestAuthorization() async -> Bool {
        guard available else { return false }
        let c = UNUserNotificationCenter.current()
        c.delegate = self
        return (try? await c.requestAuthorization(options: [.alert, .sound, .badge])) ?? false
    }

    func post(title: String, body: String, id: String = UUID().uuidString, sound: Bool = true) {
        guard available else { print("[notify] \(title): \(body)"); return }
        let content = UNMutableNotificationContent()
        content.title = title; content.body = body
        if sound { content.sound = .default }
        UNUserNotificationCenter.current().add(UNNotificationRequest(identifier: id, content: content, trigger: nil))
    }

    func schedule(id: String, title: String, body: String, at date: Date) {
        guard available else { return }
        let content = UNMutableNotificationContent()
        content.title = title; content.body = body; content.sound = .defaultCritical
        let comps = Calendar.current.dateComponents([.year, .month, .day, .hour, .minute, .second], from: date)
        let trig = UNCalendarNotificationTrigger(dateMatching: comps, repeats: false)
        UNUserNotificationCenter.current().add(UNNotificationRequest(identifier: id, content: content, trigger: trig))
    }

    func cancel(id: String) { if available { UNUserNotificationCenter.current().removePendingNotificationRequests(withIdentifiers: [id]) } }

    // Show banners even while the app is frontmost.
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification) async -> UNNotificationPresentationOptions {
        [.banner, .sound]
    }
}
