import Foundation

protocol LLMProvider {
    /// Returns the raw model text. Callers must treat it as untrusted data.
    func complete(system: String, user: String, json: Bool) async throws -> String
}

enum LLMError: LocalizedError {
    case http(Int, String), empty, notConfigured
    var errorDescription: String? {
        switch self {
        case .http(let c, let b): return "LLM request failed (\(c)): \(b.prefix(200))"
        case .empty: return "The language model returned nothing."
        case .notConfigured: return "No language model is configured."
        }
    }
}

/// Local model through Ollama's /api/chat.
struct OllamaProvider: LLMProvider {
    var baseURL: String, model: String
    func complete(system: String, user: String, json: Bool) async throws -> String {
        guard let url = URL(string: baseURL)?.appendingPathComponent("api/chat") else { throw LLMError.notConfigured }
        var req = URLRequest(url: url, timeoutInterval: 45)
        req.httpMethod = "POST"; req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        var body: [String: Any] = [
            "model": model, "stream": false,
            "messages": [["role": "system", "content": system], ["role": "user", "content": user]],
            "options": ["temperature": 0.2],
        ]
        if json { body["format"] = "json" }
        req.httpBody = try JSONSerialization.data(withJSONObject: body)
        let (data, resp) = try await URLSession.shared.data(for: req)
        if let h = resp as? HTTPURLResponse, !(200..<300).contains(h.statusCode) { throw LLMError.http(h.statusCode, String(decoding: data, as: UTF8.self)) }
        guard let o = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let m = o["message"] as? [String: Any], let c = m["content"] as? String, !c.isEmpty else { throw LLMError.empty }
        return c
    }
}

/// Any OpenAI-compatible chat API (Groq, OpenAI, OpenRouter, LM Studio...). The key is read from the Keychain.
struct OpenAICompatibleProvider: LLMProvider {
    var base: String, model: String, apiKey: String
    func complete(system: String, user: String, json: Bool) async throws -> String {
        guard !apiKey.isEmpty, let url = URL(string: base)?.appendingPathComponent("chat/completions") else { throw LLMError.notConfigured }
        var req = URLRequest(url: url, timeoutInterval: 30)
        req.httpMethod = "POST"
        req.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        var body: [String: Any] = [
            "model": model, "temperature": 0.2, "max_tokens": 700,
            "messages": [["role": "system", "content": system], ["role": "user", "content": user]],
        ]
        if json { body["response_format"] = ["type": "json_object"] }
        req.httpBody = try JSONSerialization.data(withJSONObject: body)
        let (data, resp) = try await URLSession.shared.data(for: req)
        if let h = resp as? HTTPURLResponse, !(200..<300).contains(h.statusCode) { throw LLMError.http(h.statusCode, String(decoding: data, as: UTF8.self)) }
        guard let o = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let ch = (o["choices"] as? [[String: Any]])?.first, let m = ch["message"] as? [String: Any],
              let c = m["content"] as? String, !c.isEmpty else { throw LLMError.empty }
        return c
    }
}
