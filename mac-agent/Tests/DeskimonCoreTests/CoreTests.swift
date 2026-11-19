import XCTest
@testable import DeskimonCore

final class CoreTests: XCTestCase {
    // Fixed "now": Monday 2026-10-05 10:00 in UTC
    func makeParser() -> RuleIntentParser {
        var cal = Calendar(identifier: .gregorian); cal.timeZone = TimeZone(identifier: "UTC")!
        let now = ISO8601DateFormatter().date(from: "2026-10-05T10:00:00Z")!
        return RuleIntentParser(dt: DateTimeParser(now: now, calendar: cal))
    }
    var validator: IntentValidator { IntentValidator(dt: makeParser().dt) }

    // MARK: - createEvent intent detection (regression tests for the schedule-as-list-query bug)

    /// "Schedule a meeting" must route to createEvent, not listEvents/getSchedule.
    /// Bug: the old regex `^(my )?(schedule|...)` matched "Schedule a meeting…" at the
    /// start of the string and silently rerouted it to list-events, returning
    /// "Nothing on your calendar that day." instead of creating the event.
    func testScheduleMeetingIsCreateEvent() {
        let p = makeParser()
        // The core regression: bare "schedule a meeting" verb phrase
        XCTAssertEqual(p.parse("Schedule a meeting tomorrow at 3 PM").intent, .createEvent)
        XCTAssertEqual(p.parse("Schedule a meeting on Friday at 10 AM").intent, .createEvent)
        XCTAssertEqual(p.parse("Schedule team sync tomorrow at 2 PM").intent, .createEvent)
        XCTAssertEqual(p.parse("Can you schedule a call on Monday at noon?").intent, .createEvent)
    }

    func testScheduleVerbExtractsFields() {
        let i = makeParser().parse("Schedule a meeting tomorrow at 3 PM")
        XCTAssertEqual(i.intent, .createEvent)
        XCTAssertEqual(i.date, "2026-10-06")
        XCTAssertEqual(i.time, "15:00")
        // title should not contain the verb "schedule" or article "a"
        XCTAssertFalse((i.title ?? "").lowercased().hasPrefix("schedule"))
    }

    func testScheduleVerbFridayNoon() {
        let i = makeParser().parse("Schedule a lunch on Friday at noon")
        XCTAssertEqual(i.intent, .createEvent)
        XCTAssertEqual(i.date, "2026-10-09")
        XCTAssertEqual(i.time, "12:00")
    }

    /// Standalone "schedule" or "my schedule" (noun) must still list events.
    func testScheduleNounRemainsListEvents() {
        let p = makeParser()
        // These are availability queries, not creation requests
        XCTAssertEqual(p.parse("What meetings do I have today?").intent, .listEvents)
        // "Show me my schedule" contains the word "schedule" as a noun — may resolve to
        // .listEvents or .getSchedule; both are correct list-style intents, NOT createEvent.
        let show = p.parse("Show me my schedule for tomorrow").intent
        XCTAssertTrue(show == .listEvents || show == .getSchedule,
                      "Expected a list intent but got \(show)")
        let whats = p.parse("What's on my calendar today?").intent
        XCTAssertTrue(whats == .listEvents || whats == .getSchedule,
                      "Expected a list intent but got \(whats)")
        // "My schedule" alone as a noun phrase
        let bare = p.parse("My schedule?").intent
        XCTAssertTrue(bare == .listEvents || bare == .getSchedule,
                      "Expected list/getSchedule but got \(bare)")
    }

    // MARK: - Existing createEvent tests

    func testScheduleTomorrow4PM() {
        let i = makeParser().parse("Schedule project review tomorrow at 4 PM")
        XCTAssertEqual(i.intent, .createEvent); XCTAssertEqual(i.date, "2026-10-06"); XCTAssertEqual(i.time, "16:00")
        XCTAssertEqual(i.title, "Project review")
    }
    func testScheduleBareHour() {
        let i = makeParser().parse("Schedule a meeting tomorrow at 4.")
        XCTAssertEqual(i.intent, .createEvent); XCTAssertEqual(i.time, "16:00")
    }
    func testAddOnFriday() {
        let i = makeParser().parse("Add project review on Friday at 2 pm")
        XCTAssertEqual(i.date, "2026-10-09"); XCTAssertEqual(i.time, "14:00")
    }
    func testMove() {
        let i = makeParser().parse("Move my AI project meeting to 5 PM")
        XCTAssertEqual(i.intent, .updateEvent); XCTAssertEqual(i.newTime, "17:00"); XCTAssertEqual(i.query, "ai project")
    }
    func testCancel() {
        let i = makeParser().parse("Cancel tomorrow's meeting")
        XCTAssertEqual(i.intent, .deleteEvent); XCTAssertEqual(i.date, "2026-10-06")
    }
    func testListToday() {
        let i = makeParser().parse("What meetings do I have today?")
        XCTAssertEqual(i.intent, .listEvents); XCTAssertEqual(i.date, "2026-10-05")
    }
    func testReminder() {
        let i = makeParser().parse("Remind me to submit the assignment tomorrow")
        XCTAssertEqual(i.intent, .createReminder); XCTAssertEqual(i.date, "2026-10-06"); XCTAssertEqual(i.title, "Submit the assignment")
    }
    func testRecurringReminder() {
        let i = makeParser().parse("Remind me every Monday at 9 to review my goals")
        XCTAssertEqual(i.intent, .createReminder); XCTAssertEqual(i.recurrence, .weekly); XCTAssertEqual(i.weekday, 2); XCTAssertEqual(i.time, "09:00")
    }
    func testAlarm() {
        let i = makeParser().parse("Set an alarm for 7 AM")
        XCTAssertEqual(i.intent, .createAlarm); XCTAssertEqual(i.time, "07:00"); XCTAssertEqual(i.date, "2026-10-06")
    }
    func testOpen() {
        XCTAssertEqual(makeParser().parse("Open Chrome").intent, .openApp)
        XCTAssertEqual(makeParser().parse("Open GitHub").intent, .openURL)
        XCTAssertEqual(makeParser().parse("Open Downloads").intent, .openFile)
    }
    func testEmailDraft() {
        let i = makeParser().parse("Draft an email to my professor asking for tomorrow's meeting")
        XCTAssertEqual(i.intent, .draftEmail); XCTAssertEqual(i.recipient, "professor")
    }
    func testConfirm() {
        XCTAssertEqual(makeParser().parse("yes send it").intent, .confirm)
        XCTAssertEqual(makeParser().parse("no don't send").intent, .cancel)
    }

    // MARK: - Validator: createEvent missing fields

    /// When a scheduling intent is missing the time, the validator must throw an informative error —
    /// not silently succeed and return a misleading calendar-availability message.
    func testValidatorRejectsCreateEventMissingTime() {
        var i = Intent(intent: .createEvent)
        i.title = "Team sync"; i.date = "2026-10-06"
        // time is nil — validator must reject with an actionable message
        XCTAssertThrowsError(try validator.validate(i)) { err in
            let msg = (err as? IntentValidationError).flatMap { if case .invalid(let m) = $0 { return m } else { return nil } } ?? ""
            XCTAssertFalse(msg.isEmpty, "Expected a human-readable message about what is missing")
        }
    }

    func testValidatorRejectsCreateEventMissingTitle() {
        var i = Intent(intent: .createEvent)
        i.date = "2026-10-06"; i.time = "14:00"
        XCTAssertThrowsError(try validator.validate(i))
    }

    func testValidatorRejectsCreateEventMissingBothDateAndTime() {
        var i = Intent(intent: .createEvent)
        i.title = "Standup"
        XCTAssertThrowsError(try validator.validate(i))
    }

    /// Valid complete createEvent intent must pass validation without throwing.
    func testValidatorAcceptsCompleteCreateEvent() {
        var i = Intent(intent: .createEvent)
        i.title = "Team sync"; i.date = "2026-10-06"; i.time = "14:00"
        XCTAssertNoThrow(try validator.validate(i))
    }

    // MARK: - Existing validator and protocol tests

    func testValidatorRejectsBadData() {
        var i = Intent(intent: .createEvent); i.title = "x"; i.date = "2026-02-31"; i.time = "10:00"
        XCTAssertThrowsError(try IntentValidator(dt: makeParser().dt).validate(i))
        i.date = "2026-02-10"; i.time = "25:00"
        XCTAssertThrowsError(try IntentValidator(dt: makeParser().dt).validate(i))
    }
    func testProtocolFraming() {
        var buf = Data("{\"a\":1}\n{\"b\":".utf8)
        let lines = WireProtocol.drainLines(&buf)
        XCTAssertEqual(lines.count, 1); XCTAssertEqual(String(data: buf, encoding: .utf8), "{\"b\":")
    }

    // MARK: - Date/time extraction edge cases used by scheduling

    func testExtractTimeNoon() {
        let dt = makeParser().dt
        XCTAssertEqual(dt.extractTime(from: "at noon")?.time, "12:00")
    }

    func testExtractTimeMidnight() {
        let dt = makeParser().dt
        XCTAssertEqual(dt.extractTime(from: "at midnight")?.time, "00:00")
    }

    func testExtractDateTomorrow() {
        let dt = makeParser().dt
        XCTAssertEqual(dt.extractDate(from: "tomorrow")?.date, "2026-10-06")
    }

    func testExtractDateNextFriday() {
        let dt = makeParser().dt
        // now is Monday 2026-10-05; next Friday is 2026-10-09
        XCTAssertEqual(dt.extractDate(from: "on Friday")?.date, "2026-10-09")
    }

    func testExtractDuration() {
        let dt = makeParser().dt
        XCTAssertEqual(dt.extractDuration(from: "for 30 minutes"), 30)
        XCTAssertEqual(dt.extractDuration(from: "for 1 hour"), 60)
        XCTAssertEqual(dt.extractDuration(from: "2 hrs"), 120)
    }

    // MARK: - createEvent with duration

    func testScheduleWithDuration() {
        let i = makeParser().parse("Schedule a team review tomorrow at 2 PM for 45 minutes")
        XCTAssertEqual(i.intent, .createEvent)
        XCTAssertEqual(i.date, "2026-10-06")
        XCTAssertEqual(i.time, "14:00")
        XCTAssertEqual(i.durationMinutes, 45)
    }

    // MARK: - createEvent with recurrence

    func testScheduleRecurringWeeklyEvent() {
        let i = makeParser().parse("Schedule a standup every Monday at 9 AM")
        XCTAssertEqual(i.intent, .createEvent)
        XCTAssertEqual(i.recurrence, .weekly)
        XCTAssertEqual(i.weekday, 2)   // Monday = 2
        XCTAssertEqual(i.time, "09:00")
    }
}
