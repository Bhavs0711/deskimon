import Foundation
import DeskimonCore

/// text -> Intent -> CommandRouter -> CommandResult. Deskimon (device) and the Mac UI both call `handle`,
/// so there is exactly one implementation of every action.
@MainActor
final class Assistant {
    let extractor: IntentExtractor
    let router: CommandRouter
    var log: ((String) -> Void)?

    init(extractor: IntentExtractor, router: CommandRouter) { self.extractor = extractor; self.router = router }

    func handle(_ text: String, origin: Origin) async -> CommandResult {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !clean.isEmpty else { return CommandResult(success: false, message: "I didn't catch that.", emotion: "confused", title: "Say again?", overlay: "status") }
        guard clean.count <= IntentValidator.maxText else { return .failure("That request is too long.") }
        let (intent, usedLLM, note) = await extractor.extract(clean)
        log?("[\(origin.rawValue)] \"\(clean)\" -> \(intent.intent.rawValue)\(usedLLM ? " (LLM)" : " (rules)")\(note.map { " - \($0)" } ?? "")")
        return await router.execute(intent, origin: origin)
    }
}
