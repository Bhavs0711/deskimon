import AppKit
import DeskimonCore

/// Opens apps, URLs and allow-listed files/folders with NSWorkspace / FileManager. No shell, no AppleScript "do shell".
@MainActor
struct AppLauncher {
    var policy: FilePolicy

    private static let appAliases: [String: String] = [
        "chrome": "Google Chrome", "google chrome": "Google Chrome", "safari": "Safari", "firefox": "Firefox",
        "vs code": "Visual Studio Code", "vscode": "Visual Studio Code", "visual studio code": "Visual Studio Code", "code": "Visual Studio Code",
        "terminal": "Terminal", "finder": "Finder", "mail": "Mail", "calendar": "Calendar", "notes": "Notes",
        "reminders": "Reminders", "messages": "Messages", "slack": "Slack", "zoom": "zoom.us", "teams": "Microsoft Teams",
        "xcode": "Xcode", "spotify": "Spotify", "notion": "Notion", "word": "Microsoft Word", "excel": "Microsoft Excel",
        "powerpoint": "Microsoft PowerPoint", "pycharm": "PyCharm", "intellij": "IntelliJ IDEA", "music": "Music",
        "preview": "Preview", "pages": "Pages", "numbers": "Numbers", "keynote": "Keynote", "obsidian": "Obsidian",
        "discord": "Discord", "whatsapp": "WhatsApp", "system settings": "System Settings", "settings": "System Settings",
        "calculator": "Calculator", "activity monitor": "Activity Monitor",
    ]
    private static let siteAliases: [String: String] = [
        "github": "https://github.com", "gmail": "https://mail.google.com", "google": "https://www.google.com",
        "youtube": "https://www.youtube.com", "linkedin": "https://www.linkedin.com", "stack overflow": "https://stackoverflow.com",
        "stackoverflow": "https://stackoverflow.com", "chatgpt": "https://chatgpt.com", "claude": "https://claude.ai",
        "google calendar": "https://calendar.google.com", "google drive": "https://drive.google.com", "notion": "https://www.notion.so",
        "canvas": "https://canvas.instructure.com", "leetcode": "https://leetcode.com",
    ]

    func openApp(_ name: String) async -> CommandResult {
        let key = name.lowercased().trimmingCharacters(in: .whitespaces)
        let display = Self.appAliases[key] ?? name
        let dirs = ["/Applications", "/System/Applications", "/System/Applications/Utilities", "/Applications/Utilities",
                    FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Applications").path]
        let fm = FileManager.default
        for d in dirs {
            guard let items = try? fm.contentsOfDirectory(atPath: d) else { continue }
            if let hit = items.first(where: { $0.lowercased() == display.lowercased() + ".app" }) {
                let url = URL(fileURLWithPath: d).appendingPathComponent(hit)
                do {
                    _ = try await NSWorkspace.shared.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration())
                    return CommandResult(success: true, message: "Opening \(display).", emotion: "happy", intent: "open_app", title: "Opening", overlay: "info")
                } catch { return .failure("I couldn't open \(display): \(error.localizedDescription)", intent: "open_app") }
            }
        }
        return .failure("I couldn't find an app called \(name).", intent: "open_app")
    }

    func openURL(_ target: String) -> CommandResult {
        let key = target.lowercased().trimmingCharacters(in: .whitespaces)
        var s = Self.siteAliases[key] ?? target
        if !s.contains("://") { s = "https://" + s }
        guard let url = URL(string: s), let scheme = url.scheme?.lowercased(), ["https", "http"].contains(scheme), url.host != nil else {
            return .failure("That doesn't look like a safe web address.", intent: "open_url")
        }
        NSWorkspace.shared.open(url)
        return CommandResult(success: true, message: "Opening \(url.host ?? "the page").", emotion: "happy", intent: "open_url", title: "Opening", overlay: "info")
    }

    func openFile(_ target: String) -> CommandResult {
        var url: URL? = policy.namedFolder(target)
        if url == nil, target.hasPrefix("/") || target.hasPrefix("~") { url = URL(fileURLWithPath: (target as NSString).expandingTildeInPath) }
        guard let u = url else {
            if target.lowercased().contains("project") { return .failure("Set your project folder in the Agent's Settings first.", intent: "open_file") }
            return .failure("I don't know a folder called \"\(target)\".", intent: "open_file")
        }
        if let why = policy.denial(for: u) { return .failure(why, intent: "open_file") }
        guard FileManager.default.fileExists(atPath: u.path) else { return .failure("That path doesn't exist.", intent: "open_file") }
        NSWorkspace.shared.open(u)
        return CommandResult(success: true, message: "Opening \(u.lastPathComponent).", emotion: "happy", intent: "open_file", title: "Opening", overlay: "info")
    }
}
