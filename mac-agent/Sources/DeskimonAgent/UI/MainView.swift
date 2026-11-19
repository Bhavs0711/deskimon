import SwiftUI
import DeskimonCore

struct MainView: View {
    @EnvironmentObject var model: AppModel
    @ObservedObject var hub: DeviceHub
    @ObservedObject var ble: DeviceBLE
    @ObservedObject var email: EmailService
    @ObservedObject var dictation: MacDictation
    @State private var text = ""
    @State private var ssid = UserDefaults.standard.string(forKey: "wifiSSID") ?? ""
    @State private var wifiPass = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                header
                commandBar
                if let r = model.lastResult { resultCard(r) }
                if let d = email.pending { EmailDraftCard(draft: d).environmentObject(model) }
                pairing
                logView
            }.padding(20)
        }
        .frame(minWidth: 560, minHeight: 640)
    }

    var header: some View {
        HStack(spacing: 10) {
            Text("Deskimon").font(.largeTitle.bold())
            Spacer()
            chip("Wi-Fi", hub.wifiLinked, .green)
            chip("BLE", hub.bleLinked, .blue)
            chip(hub.isConnected ? "Connected" : (hub.hasToken ? "Waiting for device" : "Not paired"), hub.isConnected, .green)
        }
    }

    func chip(_ t: String, _ on: Bool, _ c: Color) -> some View {
        Text(t).font(.caption.bold()).padding(.horizontal, 10).padding(.vertical, 5)
            .background(on ? c.opacity(0.2) : Color.gray.opacity(0.15)).foregroundStyle(on ? c : Color.secondary).clipShape(Capsule())
    }

    var commandBar: some View {
        HStack {
            TextField("Ask Deskimon: \"Schedule project review tomorrow at 4 PM\"", text: $text)
                .textFieldStyle(.roundedBorder).onSubmit { model.submit(text); text = "" }
            Button { model.dictate() } label: { Image(systemName: dictation.listening ? "mic.fill" : "mic") }
                .help("Dictate on this Mac (press Space)")
            Button("Send") { model.submit(text); text = "" }.keyboardShortcut(.return).disabled(model.busy)
        }
        .overlay(alignment: .bottomLeading) { if dictation.listening { Text(dictation.partial.isEmpty ? "Listening..." : dictation.partial).font(.caption).foregroundStyle(.secondary).offset(y: 22) } }
    }

    func resultCard(_ r: CommandResult) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(r.title ?? (r.success ? "Done" : "Problem"), systemImage: r.success ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                .foregroundStyle(r.success ? .green : .orange).font(.headline)
            Text(r.message)
            ForEach(r.detail ?? [], id: \.self) { Text("- " + $0).font(.callout).foregroundStyle(.secondary) }
            Text("Deskimon face: \(r.emotion)").font(.caption).foregroundStyle(.secondary)
        }.padding(14).frame(maxWidth: .infinity, alignment: .leading).background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 12))
    }

    var pairing: some View {
        GroupBox("Pair / connect Deskimon") {
            VStack(alignment: .leading, spacing: 8) {
                Text("1. Long-press the Deskimon screen until it says \"Pairing mode\".   2. Enter your 2.4 GHz Wi-Fi (the one this Mac is on).   3. Click Pair.")
                    .font(.callout).foregroundStyle(.secondary)
                HStack {
                    TextField("Wi-Fi name (SSID)", text: $ssid).textFieldStyle(.roundedBorder)
                    SecureField("Wi-Fi password", text: $wifiPass).textFieldStyle(.roundedBorder)
                    Button("Pair") { UserDefaults.standard.set(ssid, forKey: "wifiSSID"); hub.pair(ssid: ssid, password: wifiPass); wifiPass = "" }
                        .disabled(ssid.isEmpty)
                }
                Text(hub.pairingStatus.isEmpty ? ble.state : hub.pairingStatus).font(.caption)
                if hub.hasToken { Button("Unpair this Deskimon", role: .destructive) { hub.unpair() }.font(.caption) }
                if !hub.deviceID.isEmpty { Text("Device \(hub.deviceID) - firmware \(hub.firmware)").font(.caption).foregroundStyle(.secondary) }
            }.padding(6)
        }
    }

    var logView: some View {
        GroupBox("Activity") {
            ScrollView { Text(model.logLines.suffix(40).joined(separator: "\n")).font(.system(.caption, design: .monospaced)).frame(maxWidth: .infinity, alignment: .leading).textSelection(.enabled) }
                .frame(height: 150)
        }
    }
}

struct EmailDraftCard: View {
    @EnvironmentObject var model: AppModel
    @State var draft: EmailDraft
    init(draft: EmailDraft) { _draft = State(initialValue: draft) }

    var body: some View {
        GroupBox("Email draft - needs your approval") {
            VStack(alignment: .leading, spacing: 8) {
                HStack { Text("To").frame(width: 60, alignment: .leading); TextField("recipient@example.com", text: $draft.to).textFieldStyle(.roundedBorder) }
                HStack { Text("Subject").frame(width: 60, alignment: .leading); TextField("Subject", text: $draft.subject).textFieldStyle(.roundedBorder) }
                TextEditor(text: $draft.body).font(.body).frame(minHeight: 160).border(Color.gray.opacity(0.3))
                HStack {
                    Button("Discard", role: .cancel) { model.email.discard() }
                    Spacer()
                    Button("Send...") { model.email.pending = draft; model.sendEmailApproved() }.buttonStyle(.borderedProminent).disabled(model.busy)
                }
                Text("Nothing is sent until you confirm in the next dialog.").font(.caption).foregroundStyle(.secondary)
            }.padding(6)
        }
    }
}

struct SettingsView: View {
    @ObservedObject var settings: AppSettings
    @State private var apiKey = Keychain.load(KeychainKey.apiKey) ?? ""
    @State private var gmailSecret = Keychain.load("gmail.client.secret") ?? ""

    var body: some View {
        Form {
            Section("Language model") {
                Picker("Provider", selection: $settings.provider) { ForEach(LLMProviderKind.allCases) { Text($0.rawValue).tag($0) } }
                if settings.provider == .ollama {
                    TextField("Ollama URL", text: $settings.ollamaURL); TextField("Model", text: $settings.ollamaModel)
                }
                if settings.provider == .openAICompatible {
                    TextField("API base URL", text: $settings.apiBase); TextField("Model", text: $settings.apiModel)
                    SecureField("API key (stored in Keychain)", text: $apiKey).onChange(of: apiKey) { _, v in Keychain.save(KeychainKey.apiKey, v) }
                }
            }
            Section("Email") {
                Picker("Send with", selection: $settings.mailMethod) { ForEach(MailMethod.allCases) { Text($0.rawValue).tag($0) } }
                if settings.mailMethod == .gmailOAuth {
                    TextField("Google OAuth client ID (Desktop app)", text: $settings.gmailClientID)
                    SecureField("Client secret (Keychain)", text: $gmailSecret).onChange(of: gmailSecret) { _, v in Keychain.save("gmail.client.secret", v) }
                }
            }
            Section("Voice") {
                Toggle("Speak replies on this Mac", isOn: $settings.speakOnMac)
                Toggle("Speak replies on Deskimon", isOn: $settings.speakOnDevice)
                TextField("Kokoro server URL", text: $settings.kokoroURL)
            }
            Section("Work") {
                TextField("Project folder (for \"open my project folder\")", text: $settings.projectFolder)
                Stepper("Default event length: \(settings.defaultEventMinutes) min", value: $settings.defaultEventMinutes, in: 15...240, step: 15)
            }
        }.formStyle(.grouped).frame(width: 520, height: 520)
    }
}
