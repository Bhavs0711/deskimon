import Foundation
import DeskimonCore

/// Natural language -> structured Intent. The LLM only *proposes* parameters; they are decoded into a closed
/// schema, validated by IntentValidator and executed (or refused) by the CommandRouter. If the model is
/// unavailable or returns garbage we fall back to the offline rule parser.
@MainActor
final class IntentExtractor {
    private let settings: AppSettings
    init(settings: AppSettings) { self.settings = settings }

    static let persona = """
    You are DESKIMON, a friendly, concise, intelligent AI desk assistant that lives on a small round desk device \
    connected to the user's Mac. You help with work: college, coding, projects, meetings, productivity, assignments, \
    schedules, deadlines, emails and planning. For unrelated casual questions give a very short friendly answer and \
    steer back to productive work. Never be rude or robotic. Never use markdown, emoji or asterisks. Replies are at \
    most two short sentences because they will be spoken aloud.
    """

    private func system(now: Date) -> String {
        let f = DateFormatter(); f.dateFormat = "EEEE yyyy-MM-dd HH:mm"; f.locale = Locale(identifier: "en_US_POSIX")
        let tz = TimeZone.current.identifier
        let intents = IntentName.allCases.map(\.rawValue).joined(separator: ", ")
        let emotions = DeskimonEmotion.all.sorted().joined(separator: ", ")
        return """
        \(Self.persona)

        Convert the user's request into ONE JSON object and output nothing else.
        Current local time: \(f.string(from: now)) (\(tz)). Weekday numbers: 1=Sunday ... 7=Saturday.
        Schema (omit fields that do not apply):
        {"intent": one of [\(intents)],
         "title": string, "date": "yyyy-MM-dd", "time": "HH:mm" (24h), "end_time": "HH:mm", "duration_minutes": int,
         "new_date": "yyyy-MM-dd", "new_time": "HH:mm", "query": string (words identifying an existing event),
         "range_days": int, "recurrence": "daily"|"weekdays"|"weekly"|"monthly", "weekday": int,
         "recipient": string, "subject": string, "body": string (what the email should say, in the user's words),
         "target": string (app name, website or folder name), "location": string, "notes": string,
         "reply": string (for general_work_query: the short spoken answer; for others a short confirmation),
         "emotion": one of [\(emotions)]}
        Rules: resolve relative dates ("tomorrow", "Friday") to absolute dates. Meetings without a stated time are \
        NOT assumed. Use "confirm"/"cancel" only for replies like "yes send it"/"no cancel". You cannot run shell \
        commands, delete files or send email directly; emails are only drafted (intent draft_email). \
        Anything else is general_work_query with a helpful short "reply".
        """
    }

    private func provider() -> LLMProvider? {
        switch settings.provider {
        case .rulesOnly: return nil
        case .ollama: return OllamaProvider(baseURL: settings.ollamaURL, model: settings.ollamaModel)
        case .openAICompatible:
            guard let k = Keychain.load(KeychainKey.apiKey), !k.isEmpty else { return nil }
            return OpenAICompatibleProvider(base: settings.apiBase, model: settings.apiModel, apiKey: k)
        }
    }

    func extract(_ text: String, now: Date = Date()) async -> (intent: Intent, usedLLM: Bool, note: String?) {
        let dt = DateTimeParser(now: now)
        let rules = RuleIntentParser(dt: dt).parse(text)
        // Obvious action phrases are parsed deterministically (fast, offline, no hallucinated dates).
        if rules.intent != .generalWorkQuery, (try? IntentValidator(dt: dt).validate(rules)) != nil, settings.provider == .rulesOnly {
            return (rules, false, nil)
        }
        guard let llm = provider() else { return (rules, false, settings.provider == .rulesOnly ? nil : "No language model available - used offline rules.") }
        do {
            let raw = try await llm.complete(system: system(now: now), user: text, json: true)
            if var i = Self.decode(raw) {
                i.sourceText = text
                if (try? IntentValidator(dt: dt).validate(i)) != nil || i.intent == .generalWorkQuery { return (i, true, nil) }
                // The LLM output failed validation; prefer the rule parse if it is valid.
                if (try? IntentValidator(dt: dt).validate(rules)) != nil, rules.intent != .generalWorkQuery { return (rules, false, "Model output failed validation - used rules.") }
                return (i, true, nil)    // let the router report what is missing
            }
        } catch { return (rules, false, "Language model unavailable (\(error.localizedDescription)) - used offline rules.") }
        return (rules, false, "Couldn't read the model's answer - used offline rules.")
    }

    static func decode(_ raw: String) -> Intent? {
        var s = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if s.hasPrefix("```") { s = s.replacingOccurrences(of: "```json", with: "").replacingOccurrences(of: "```", with: "") }
        guard let a = s.firstIndex(of: "{"), let b = s.lastIndex(of: "}"), a < b else { return nil }
        let json = Data(s[a...b].utf8)
        // Tolerate numeric strings and nulls from small models by normalising through JSONSerialization first.
        guard var obj = (try? JSONSerialization.jsonObject(with: json)) as? [String: Any] else { return nil }
        for (k, v) in obj { if v is NSNull { obj.removeValue(forKey: k) } }
        for k in ["duration_minutes", "range_days", "weekday"] { if let str = obj[k] as? String, let n = Int(str) { obj[k] = n } }
        for k in ["recurrence"] { if let str = obj[k] as? String, Recurrence(rawValue: str.lowercased()) == nil { obj.removeValue(forKey: k) } }
        guard let data = try? JSONSerialization.data(withJSONObject: obj) else { return nil }
        return try? JSONDecoder().decode(Intent.self, from: data)
    }

    /// Generate an email draft (subject + body) from the user's instruction.
    func draftEmail(instruction: String, recipient: String?) async -> (subject: String, body: String) {
        if let llm = provider() {
            let sys = """
            \(Self.persona)
            Write a professional, polite, concise email for the user. Output ONLY JSON: {"subject": string, "body": string}. \
            The body starts with a greeting and ends with a closing; sign off with just "Best regards," on one line and leave the \
            name for the user to add. Do not invent facts, dates or attachments not mentioned.
            """
            let user = "Recipient: \(recipient ?? "unknown")\nInstruction: \(instruction)"
            if let raw = try? await llm.complete(system: sys, user: user, json: true),
               let a = raw.firstIndex(of: "{"), let b = raw.lastIndex(of: "}"),
               let o = (try? JSONSerialization.jsonObject(with: Data(raw[a...b].utf8))) as? [String: Any],
               let subj = o["subject"] as? String, let body = o["body"] as? String, !body.isEmpty { return (subj, body) }
        }
        // Offline template
        let topic = instruction.replacingOccurrences(of: #"(?i)^.*?\b(asking|about|regarding|saying|requesting|telling)\b\s*(for|if|whether)?\s*"#, with: "", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let subj = topic.isEmpty ? "Quick question" : String(topic.prefix(60)).prefix(1).uppercased() + String(topic.prefix(60)).dropFirst()
        let name = recipient.flatMap { $0.contains("@") ? nil : $0.capitalized } ?? "there"
        return (subj, "Hello \(name),\n\nI hope you're doing well. I'm writing to ask about \(topic.isEmpty ? "a quick matter" : topic). Could you let me know what works for you?\n\nThank you for your time.\n\nBest regards,")
    }
}
