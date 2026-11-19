import Foundation
import DeskimonCore

enum LLMProviderKind: String, CaseIterable, Identifiable {
    case rulesOnly = "Offline rules only"
    case ollama = "Ollama (local)"
    case openAICompatible = "API (Groq / OpenAI-compatible)"
    var id: String { rawValue }
}

enum MailMethod: String, CaseIterable, Identifiable {
    case mailApp = "Apple Mail"
    case gmailOAuth = "Gmail (OAuth)"
    var id: String { rawValue }
}

/// User-tunable settings. Secrets (API keys, OAuth tokens, device pairing token) live in the Keychain, never here.
@MainActor
final class AppSettings: ObservableObject {
    private let d = UserDefaults.standard
    @Published var provider: LLMProviderKind { didSet { d.set(provider.rawValue, forKey: "provider") } }
    @Published var ollamaURL: String { didSet { d.set(ollamaURL, forKey: "ollamaURL") } }
    @Published var ollamaModel: String { didSet { d.set(ollamaModel, forKey: "ollamaModel") } }
    @Published var apiBase: String { didSet { d.set(apiBase, forKey: "apiBase") } }
    @Published var apiModel: String { didSet { d.set(apiModel, forKey: "apiModel") } }
    @Published var projectFolder: String { didSet { d.set(projectFolder, forKey: "projectFolder") } }
    @Published var mailMethod: MailMethod { didSet { d.set(mailMethod.rawValue, forKey: "mailMethod") } }
    @Published var gmailClientID: String { didSet { d.set(gmailClientID, forKey: "gmailClientID") } }
    @Published var speakOnMac: Bool { didSet { d.set(speakOnMac, forKey: "speakOnMac") } }
    @Published var speakOnDevice: Bool { didSet { d.set(speakOnDevice, forKey: "speakOnDevice") } }
    @Published var kokoroURL: String { didSet { d.set(kokoroURL, forKey: "kokoroURL") } }
    @Published var defaultEventMinutes: Int { didSet { d.set(defaultEventMinutes, forKey: "defaultEventMinutes") } }

    init() {
        provider = LLMProviderKind(rawValue: d.string(forKey: "provider") ?? "") ?? .rulesOnly
        ollamaURL = d.string(forKey: "ollamaURL") ?? "http://127.0.0.1:11434"
        ollamaModel = d.string(forKey: "ollamaModel") ?? "llama3.2"
        apiBase = d.string(forKey: "apiBase") ?? "https://api.groq.com/openai/v1"
        apiModel = d.string(forKey: "apiModel") ?? "llama-3.3-70b-versatile"
        projectFolder = d.string(forKey: "projectFolder") ?? ""
        mailMethod = MailMethod(rawValue: d.string(forKey: "mailMethod") ?? "") ?? .mailApp
        gmailClientID = d.string(forKey: "gmailClientID") ?? ""
        speakOnMac = d.object(forKey: "speakOnMac") as? Bool ?? true
        speakOnDevice = d.object(forKey: "speakOnDevice") as? Bool ?? true
        kokoroURL = d.string(forKey: "kokoroURL") ?? "http://127.0.0.1:8000/tts"
        defaultEventMinutes = d.object(forKey: "defaultEventMinutes") as? Int ?? 60
    }
}
