import Foundation
import AppKit
import Contacts
import Network
import CryptoKit
import DeskimonCore

struct EmailDraft: Identifiable, Equatable {
    let id = UUID()
    var to: String
    var subject: String
    var body: String
}

enum EmailError: LocalizedError {
    case noRecipient, badRecipient, mailScript(String), oauth(String)
    var errorDescription: String? {
        switch self {
        case .noRecipient: return "The draft has no recipient yet. Add one in the Agent window."
        case .badRecipient: return "That recipient isn't a valid email address."
        case .mailScript(let m): return "Apple Mail refused the request: \(m)"
        case .oauth(let m): return "Gmail sign-in problem: \(m)"
        }
    }
}

/// Email drafting + sending. Sending ALWAYS needs explicit approval (the Agent window's Send button followed by a
/// confirmation dialog). No credentials are stored in code; Gmail uses OAuth2 (PKCE) with the refresh token in the Keychain,
/// Apple Mail uses the accounts already signed in on the Mac.
@MainActor
final class EmailService: ObservableObject {
    @Published var pending: EmailDraft?
    private let settings: AppSettings
    private let extractor: IntentExtractor

    init(settings: AppSettings, extractor: IntentExtractor) { self.settings = settings; self.extractor = extractor }

    func createDraft(from intent: Intent) async -> EmailDraft {
        var to = intent.recipient ?? ""
        if !IntentValidator.isEmail(to), !to.isEmpty, let resolved = await Self.lookupContact(named: to) { to = resolved }
        let instruction = intent.body ?? intent.sourceText ?? ""
        let (subject, body) = await extractor.draftEmail(instruction: instruction, recipient: intent.recipient)
        let d = EmailDraft(to: IntentValidator.isEmail(to) ? to : "", subject: intent.subject ?? subject, body: body)
        pending = d
        return d
    }

    static func lookupContact(named name: String) async -> String? {
        let store = CNContactStore()
        guard (try? await store.requestAccess(for: .contacts)) == true else { return nil }
        let keys = [CNContactEmailAddressesKey as CNKeyDescriptor, CNContactGivenNameKey as CNKeyDescriptor, CNContactFamilyNameKey as CNKeyDescriptor]
        let pred = CNContact.predicateForContacts(matchingName: name)
        guard let hits = try? store.unifiedContacts(matching: pred, keysToFetch: keys), hits.count == 1,
              let email = hits[0].emailAddresses.first?.value as String? else { return nil }
        return email
    }

    func discard() { pending = nil }

    func send(_ d: EmailDraft) async throws {
        let to = d.to.trimmingCharacters(in: .whitespaces)
        guard !to.isEmpty else { throw EmailError.noRecipient }
        guard IntentValidator.isEmail(to) else { throw EmailError.badRecipient }
        switch settings.mailMethod {
        case .mailApp: try Self.sendWithMailApp(to: to, subject: d.subject, body: d.body)
        case .gmailOAuth: try await GmailSender(clientID: settings.gmailClientID).send(to: to, subject: d.subject, body: d.body)
        }
        if pending?.id == d.id { pending = nil }
    }

    private static func asLiteral(_ s: String) -> String {
        let e = s.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        return "\"" + e.replacingOccurrences(of: "\r\n", with: "\n").replacingOccurrences(of: "\n", with: "\" & linefeed & \"") + "\""
    }

    /// Uses Apple Mail's scripting interface (macOS asks for Automation permission the first time).
    static func sendWithMailApp(to: String, subject: String, body: String) throws {
        let src = """
        tell application "Mail"
            set m to make new outgoing message with properties {subject:\(asLiteral(subject)), content:\(asLiteral(body)), visible:false}
            tell m to make new to recipient at end of to recipients with properties {address:\(asLiteral(to))}
            send m
        end tell
        """
        var err: NSDictionary?
        NSAppleScript(source: src)?.executeAndReturnError(&err)
        if let err { throw EmailError.mailScript((err[NSAppleScript.errorMessage] as? String) ?? "unknown error") }
    }
}

/// Gmail API sender using OAuth2 authorization-code + PKCE with a loopback redirect (Google "Desktop app" client).
final class GmailSender {
    let clientID: String
    private var secret: String? { Keychain.load("gmail.client.secret") }
    init(clientID: String) { self.clientID = clientID }

    func send(to: String, subject: String, body: String) async throws {
        guard !clientID.isEmpty else { throw EmailError.oauth("Add your Google OAuth client ID in Settings first.") }
        let token = try await accessToken()
        let mime = "To: \(to)\r\nSubject: =?UTF-8?B?\(Data(subject.utf8).base64EncodedString())?=\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n\(body)"
        let raw = Data(mime.utf8).base64EncodedString().replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
        var req = URLRequest(url: URL(string: "https://gmail.googleapis.com/gmail/v1/users/me/messages/send")!)
        req.httpMethod = "POST"; req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization"); req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try JSONSerialization.data(withJSONObject: ["raw": raw])
        let (data, resp) = try await URLSession.shared.data(for: req)
        guard (resp as? HTTPURLResponse)?.statusCode == 200 else { throw EmailError.oauth("Gmail rejected the message: \(String(decoding: data, as: UTF8.self).prefix(160))") }
    }

    private func accessToken() async throws -> String {
        if let refresh = Keychain.load(KeychainKey.gmailRefresh) {
            if let t = try? await tokenRequest(["grant_type": "refresh_token", "refresh_token": refresh, "client_id": clientID]) { if let a = t["access_token"] as? String { return a } }
        }
        return try await interactiveSignIn()
    }

    private func tokenRequest(_ params: [String: String]) async throws -> [String: Any] {
        var p = params; if let s = secret, !s.isEmpty { p["client_secret"] = s }
        var req = URLRequest(url: URL(string: "https://oauth2.googleapis.com/token")!)
        req.httpMethod = "POST"; req.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
        var c = URLComponents(); c.queryItems = p.map { URLQueryItem(name: $0.key, value: $0.value) }
        req.httpBody = Data((c.percentEncodedQuery ?? "").utf8)
        let (data, _) = try await URLSession.shared.data(for: req)
        guard let o = try JSONSerialization.jsonObject(with: data) as? [String: Any], o["error"] == nil else { throw EmailError.oauth("token request failed") }
        return o
    }

    private func interactiveSignIn() async throws -> String {
        let verifier = DeviceAuth.randomHex(bytes: 32)
        let challenge = Data(SHA256.hash(data: Data(verifier.utf8))).base64EncodedString()
            .replacingOccurrences(of: "+", with: "-").replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: "=", with: "")
        let port: UInt16 = 8789
        let redirect = "http://127.0.0.1:\(port)"
        var c = URLComponents(string: "https://accounts.google.com/o/oauth2/v2/auth")!
        c.queryItems = [
            .init(name: "client_id", value: clientID), .init(name: "redirect_uri", value: redirect), .init(name: "response_type", value: "code"),
            .init(name: "scope", value: "https://www.googleapis.com/auth/gmail.send"), .init(name: "code_challenge", value: challenge),
            .init(name: "code_challenge_method", value: "S256"), .init(name: "access_type", value: "offline"), .init(name: "prompt", value: "consent"),
        ]
        let code: String = try await withCheckedThrowingContinuation { cont in
            guard let listener = try? NWListener(using: .tcp, on: NWEndpoint.Port(rawValue: port)!) else { cont.resume(throwing: EmailError.oauth("port \(port) busy")); return }
            var done = false
            listener.newConnectionHandler = { conn in
                conn.start(queue: .global())
                conn.receive(minimumIncompleteLength: 1, maximumLength: 8192) { data, _, _, _ in
                    let req = String(decoding: data ?? Data(), as: UTF8.self)
                    let html = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n<html><body style='font-family:-apple-system;text-align:center;margin-top:20%'><h2>Deskimon is connected to Gmail.</h2>You can close this tab.</body></html>"
                    conn.send(content: Data(html.utf8), completion: .contentProcessed { _ in conn.cancel() })
                    guard !done else { return }; done = true; listener.cancel()
                    if let line = req.split(separator: "\r\n").first, let q = line.split(separator: " ").dropFirst().first,
                       let comps = URLComponents(string: "http://x" + q), let code = comps.queryItems?.first(where: { $0.name == "code" })?.value {
                        cont.resume(returning: code)
                    } else { cont.resume(throwing: EmailError.oauth("sign-in was cancelled")) }
                }
            }
            listener.start(queue: .global())
            DispatchQueue.main.async { NSWorkspace.shared.open(c.url!) }
        }
        let t = try await tokenRequest(["grant_type": "authorization_code", "code": code, "client_id": clientID, "redirect_uri": redirect, "code_verifier": verifier])
        if let r = t["refresh_token"] as? String { Keychain.save(KeychainKey.gmailRefresh, r) }
        guard let a = t["access_token"] as? String else { throw EmailError.oauth("no access token") }
        return a
    }
}
