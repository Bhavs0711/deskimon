import Foundation
import AVFoundation
import Speech

/// Kokoro TTS (local server, tools/kokoro_server.py). Model files: models/kokoro/kokoro-v1.0.onnx + voices-v1.0.bin.
struct KokoroClient {
    var url: String
    var voice = "af_bella"

    /// Raw 16-bit mono PCM at the requested rate (16000 for the Deskimon speaker, 24000 for the Mac).
    func pcm(_ text: String, rate: Int) async -> Data? {
        guard let u = URL(string: url) else { return nil }
        var req = URLRequest(url: u, timeoutInterval: 25)
        req.httpMethod = "POST"; req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try? JSONSerialization.data(withJSONObject: ["text": text, "voice": voice, "format": rate == 16000 ? "pcm16k" : "pcm24k"])
        guard let (d, r) = try? await URLSession.shared.data(for: req), (r as? HTTPURLResponse)?.statusCode == 200, d.count > 100 else { return nil }
        return d
    }
}

@MainActor
final class MacSpeaker: NSObject {
    private var player: AVAudioPlayer?
    private let synth = AVSpeechSynthesizer()
    var kokoro: KokoroClient

    init(kokoro: KokoroClient) { self.kokoro = kokoro }

    func speak(_ text: String) async {
        if let pcm = await kokoro.pcm(text, rate: 24000), let wav = Self.wav(pcm: pcm, rate: 24000), let p = try? AVAudioPlayer(data: wav) {
            player = p; p.play(); return
        }
        let u = AVSpeechUtterance(string: text)       // fallback when the Kokoro server is not running
        u.voice = AVSpeechSynthesisVoice(language: "en-US"); u.rate = 0.5
        synth.speak(u)
    }

    func stop() { player?.stop(); synth.stopSpeaking(at: .immediate) }

    static func wav(pcm: Data, rate: Int) -> Data? {
        var d = Data()
        func le32(_ v: UInt32) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 4)) }
        func le16(_ v: UInt16) { var x = v.littleEndian; d.append(Data(bytes: &x, count: 2)) }
        d.append("RIFF".data(using: .ascii)!); le32(UInt32(36 + pcm.count)); d.append("WAVEfmt ".data(using: .ascii)!)
        le32(16); le16(1); le16(1); le32(UInt32(rate)); le32(UInt32(rate * 2)); le16(2); le16(16)
        d.append("data".data(using: .ascii)!); le32(UInt32(pcm.count)); d.append(pcm)
        return d
    }
}

/// Speech-to-text for audio streamed from the Deskimon microphone (16 kHz PCM16 mono).
enum DeviceSTT {
    static func requestPermission() async -> Bool {
        await withCheckedContinuation { c in SFSpeechRecognizer.requestAuthorization { c.resume(returning: $0 == .authorized) } }
    }

    static func transcribe(pcm16: Data, rate: Double = 16000) async -> String? {
        guard let rec = SFSpeechRecognizer(locale: Locale(identifier: "en-US")), rec.isAvailable,
              let fmt = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: rate, channels: 1, interleaved: true) else { return nil }
        let req = SFSpeechAudioBufferRecognitionRequest()
        req.shouldReportPartialResults = false
        let frames = AVAudioFrameCount(pcm16.count / 2)
        guard frames > 1600, let buf = AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: frames) else { return nil }
        buf.frameLength = frames
        pcm16.withUnsafeBytes { raw in
            if let src = raw.baseAddress, let dst = buf.int16ChannelData?[0] { memcpy(dst, src, Int(frames) * 2) }
        }
        req.append(buf); req.endAudio()
        return await withCheckedContinuation { (cont: CheckedContinuation<String?, Never>) in
            var finished = false
            let task = rec.recognitionTask(with: req) { result, error in
                guard !finished else { return }
                if let r = result, r.isFinal { finished = true; cont.resume(returning: r.bestTranscription.formattedString) }
                else if error != nil { finished = true; cont.resume(returning: nil) }
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 12) { if !finished { finished = true; task.cancel(); cont.resume(returning: nil) } }
        }
    }
}

/// Mac microphone dictation (push to talk from the Agent window).
@MainActor
final class MacDictation: ObservableObject {
    @Published var listening = false
    @Published var partial = ""
    private let engine = AVAudioEngine()
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private var silenceTimer: Timer?

    func start(onFinal: @escaping (String) -> Void) {
        guard !listening, let rec = SFSpeechRecognizer(locale: Locale(identifier: "en-US")), rec.isAvailable else { return }
        let req = SFSpeechAudioBufferRecognitionRequest(); req.shouldReportPartialResults = true
        request = req
        let input = engine.inputNode
        input.removeTap(onBus: 0)
        input.installTap(onBus: 0, bufferSize: 2048, format: input.outputFormat(forBus: 0)) { [weak req] b, _ in req?.append(b) }
        engine.prepare()
        do { try engine.start() } catch { return }
        listening = true; partial = ""
        func bump(_ me: MacDictation) {
            me.silenceTimer?.invalidate()
            me.silenceTimer = Timer.scheduledTimer(withTimeInterval: 1.4, repeats: false) { _ in Task { @MainActor in me.finish(onFinal) } }
        }
        bump(self)
        task = rec.recognitionTask(with: req) { [weak self] result, error in
            Task { @MainActor in
                guard let self, self.listening else { return }
                if let r = result { self.partial = r.bestTranscription.formattedString; bump(self); if r.isFinal { self.finish(onFinal) } }
                else if error != nil { self.finish(onFinal) }
            }
        }
    }

    func finish(_ onFinal: (String) -> Void) {
        guard listening else { return }
        listening = false
        silenceTimer?.invalidate()
        engine.stop(); engine.inputNode.removeTap(onBus: 0)
        request?.endAudio(); task?.cancel(); request = nil; task = nil
        let t = partial.trimmingCharacters(in: .whitespacesAndNewlines)
        partial = ""
        if !t.isEmpty { onFinal(t) }
    }
}
