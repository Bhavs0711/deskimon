import Foundation

/// Decides which files/folders Deskimon may open. There is NO shell execution anywhere in the agent.
struct FilePolicy {
    let home = FileManager.default.homeDirectoryForCurrentUser
    var projectFolder: String

    private static let blockedExtensions: Set<String> = [
        "app", "command", "sh", "zsh", "bash", "pkg", "dmg", "terminal", "workflow", "scpt", "scptd",
        "action", "tool", "jar", "exe", "bat", "py", "pl", "rb", "js", "mpkg", "osax", "dylib", "kext",
    ]
    private static let blockedHomeDirs: Set<String> = [".ssh", ".gnupg", ".aws", ".config", ".kube", "Library"]

    func namedFolder(_ name: String) -> URL? {
        let n = name.lowercased().replacingOccurrences(of: #"\b(my|the|folder|directory|please)\b"#, with: "", options: .regularExpression)
            .trimmingCharacters(in: .whitespaces)
        let fm = FileManager.default
        switch n {
        case "downloads": return fm.urls(for: .downloadsDirectory, in: .userDomainMask).first
        case "documents": return fm.urls(for: .documentDirectory, in: .userDomainMask).first
        case "desktop": return fm.urls(for: .desktopDirectory, in: .userDomainMask).first
        case "home": return home
        case "project", "projects", "my project", "work":
            return projectFolder.isEmpty ? nil : URL(fileURLWithPath: (projectFolder as NSString).expandingTildeInPath)
        default: return nil
        }
    }

    /// Returns nil if allowed, or a human-readable reason if blocked.
    func denial(for url: URL) -> String? {
        let std = url.standardizedFileURL.resolvingSymlinksInPath()
        let homePath = home.standardizedFileURL.resolvingSymlinksInPath().path
        guard std.path == homePath || std.path.hasPrefix(homePath + "/") else { return "I can only open things inside your home folder." }
        let rel = std.path.dropFirst(homePath.count).split(separator: "/")
        if let first = rel.first, Self.blockedHomeDirs.contains(String(first)) { return "That location is protected." }
        if rel.contains(where: { $0.hasPrefix(".") }) { return "I don't open hidden files." }
        if Self.blockedExtensions.contains(std.pathExtension.lowercased()) { return "I won't open executable or script files." }
        return nil
    }
}
