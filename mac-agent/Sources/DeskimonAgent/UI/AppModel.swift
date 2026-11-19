import Foundation
import SwiftUI
import AppKit
import DeskimonCore

@MainActor
final class AppModel: ObservableObject {
    let settings = AppSettings()
    let hub = DeviceHub()
    let alarms = AlarmService()
    let dictation = MacDictation()
    let calendar = CalendarService()
    let reminders = ReminderService()
    let extractor: IntentExtractor
    let email: EmailService
    let router: CommandRouter
    let assistant: Assistant
    let speaker: MacSpeaker
    var monitor: ProactiveMonitor?

    @Published var logLines: [String] = []
    @Published var lastResult: CommandResult?
    @Published var busy = false
    @Published var permissions: [String: Bool] = [:]

    init() {
        extractor = IntentExtractor(settings: settings)
        email = EmailService(settings: settings, extractor: extractor)
        router = CommandRouter(calendar: calendar, reminders: reminders, alarms: alarms, email: email, settings: settings)
        assistant = Assistant(extractor: extractor, router: router)
        speaker = MacSpeaker(kokoro: KokoroClient(url: settings.kokoroURL))
        hub.assistant = assistant; hub.settings = settings; hub.speaker = speaker
        assistant.log = { [weak self] in self?.addLog($0) }
        hub.log = { [weak self] in self?.addLog($0) }
        hub.onDeviceCommand = { [weak self] text, r in self?.lastResult = r; self?.addLog("device: \"\(text)\" -> \(r.message)") }
        hub.onDismiss = { [weak self] in self?.alarms.stopRinging() }
        alarms.onFire = { [weak self] a in
            NotificationService.shared.post(title: "Alarm", body: a.label)
            self?.hub.sendAlert(kind: "alarm", title: a.label, subtitle: "Tap to dismiss")
            self?.addLog("alarm fired: \(a.label)")
        }
    }

    func start() {
        hub.start()
        monitor = ProactiveMonitor(calendar: calendar, reminders: reminders, hub: hub)
        Task {
            permissions["Notifications"] = await NotificationService.shared.requestAuthorization()
            permissions["Calendar"] = await calendar.requestAccess()
            permissions["Reminders"] = await reminders.requestAccess()
            permissions["Speech"] = await DeviceSTT.requestPermission()
            monitor?.start()
            addLog("Permissions: " + permissions.map { "\($0.key)=\($0.value ? "ok" : "DENIED")" }.sorted().joined(separator: ", "))
        }
        // Space bar toggles mic dictation — works from anywhere in the app
        NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if event.keyCode == 49 {  // space bar
                self?.dictate()
                return nil  // consume the event
            }
            return event
        }
    }

    func addLog(_ s: String) {
        let f = DateFormatter(); f.dateFormat = "HH:mm:ss"
        logLines.append("\(f.string(from: Date()))  \(s)")
        if logLines.count > 300 { logLines.removeFirst(logLines.count - 300) }
    }

    /// Mac-originated command: same Assistant/CommandRouter as the device, mirrored to the device display.
    func submit(_ text: String) {
        let t = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !t.isEmpty, !busy else { return }
        busy = true
        hub.mirrorMacCommand(running: true)
        Task {
            let r = await assistant.handle(t, origin: .mac)
            lastResult = r; busy = false
            hub.mirrorMacCommand(running: false, result: r)
            await hub.speakResult(r)
        }
    }

    func dictate() {
        if dictation.listening { dictation.finish { [weak self] t in self?.submit(t) }; return }
        hub.sendState("listening")
        dictation.start { [weak self] t in self?.submit(t) }
    }

    func sendEmailApproved() {
        guard let d = email.pending else { return }
        busy = true
        Task {
            let r = await router.execute(Intent(intent: .sendEmail), origin: .mac)
            _ = d
            lastResult = r; busy = false
            hub.mirrorMacCommand(running: false, result: r)
        }
    }
}
