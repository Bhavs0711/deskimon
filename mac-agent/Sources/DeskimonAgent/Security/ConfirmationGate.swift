import AppKit

/// Sensitive operations (sending email, deleting events, destructive actions) always require an explicit
/// click on the Mac, no matter whether the request came from the Deskimon device or the Mac UI.
@MainActor
enum ConfirmationGate {
    static func confirm(title: String, detail: String, confirmButton: String, destructive: Bool = false) async -> Bool {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = detail
        alert.alertStyle = destructive ? .critical : .warning
        let ok = alert.addButton(withTitle: confirmButton)
        if destructive { ok.hasDestructiveAction = true }
        alert.addButton(withTitle: "Cancel")
        return alert.runModal() == .alertFirstButtonReturn
    }
}
