import Foundation

/// Offline natural-language -> Intent parser. Handles the common productivity phrases without any LLM,
/// and acts as the fallback when Ollama / the API provider is unavailable.
public struct RuleIntentParser: Sendable {
    public var dt: DateTimeParser
    public init(dt: DateTimeParser = DateTimeParser()) { self.dt = dt }

    private func has(_ s: String, _ pattern: String) -> Bool { s.range(of: pattern, options: .regularExpression) != nil }

    public func parse(_ raw: String) -> Intent {
        let text = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        let lower = text.lowercased()
        var i = Intent(intent: .generalWorkQuery)
        i.sourceText = text

        // ---- confirmations
        if has(lower, #"^(yes|yep|yeah|confirm|confirmed|send it|go ahead|do it|approve|ok send|okay send)\b"#) { i.intent = .confirm; return i }
        if has(lower, #"^(no|nope|cancel that|don'?t send|discard|never ?mind|stop|abort)\b"#) { i.intent = .cancel; return i }

        // ---- open things
        if has(lower, #"^(please\s+)?(open|launch|start)\b"#) {
            let target = lower.replacingOccurrences(of: #"^(please\s+)?(open|launch|start)\s+(up\s+)?(the\s+)?"#, with: "", options: .regularExpression)
                .trimmingCharacters(in: .whitespaces)
            i.target = target
            if has(target, #"^https?://"#) || has(target, #"\.(com|org|io|net|edu|dev|ai)\b"#) || target == "github" || target == "gmail" || target == "youtube" || target == "google" || target == "linkedin" { i.intent = .openURL }
            else if has(target, #"(folder|downloads|documents|desktop|project|directory)"#) { i.intent = .openFile }
            else { i.intent = .openApp }
            return i
        }

        // ---- email
        if has(lower, #"\b(draft|write|compose|prepare)\b.*\b(e-?mail|mail|message)\b"#) || has(lower, #"^(e-?mail|mail)\s+\S+"#) {
            i.intent = .draftEmail
            if let r = lower.range(of: #"\bto\s+(my\s+)?([a-z0-9._%+\-]+@[a-z0-9.\-]+\.[a-z]{2,}|[a-z][a-z .'\-]{1,40}?)(?=\s+(asking|about|regarding|saying|to ask|for|that|requesting|telling|and)\b|$)"#, options: .regularExpression) {
                var rec = String(lower[r]).replacingOccurrences(of: #"^to\s+(my\s+)?"#, with: "", options: .regularExpression)
                rec = rec.trimmingCharacters(in: .whitespaces)
                i.recipient = rec
            }
            if let r = lower.range(of: #"\b(asking|about|regarding|saying|requesting|telling)\b.*$"#, options: .regularExpression) {
                i.body = String(text[r.lowerBound...])
            } else { i.body = text }
            return i
        }
        if has(lower, #"\bsend (the |that |this )?(draft|e-?mail)\b"#) { i.intent = .sendEmail; return i }

        // ---- alarm
        if has(lower, #"\b(alarm|wake me)\b"#) {
            i.intent = .createAlarm
            let d = dt.extractDate(from: text)
            if let t = dt.extractTime(from: d?.rest ?? text) { i.time = t.time }
            if let d {
                i.date = d.date
            } else if let t = i.time, let candidate = dt.combine(date: dt.dateString(dt.now), time: t) {
                // next occurrence of that time: today if still ahead, otherwise tomorrow
                let tomorrow = dt.calendar.date(byAdding: .day, value: 1, to: dt.now) ?? dt.now
                i.date = candidate > dt.now ? dt.dateString(dt.now) : dt.dateString(tomorrow)
            }
            i.title = "Alarm"
            return i
        }

        // ---- reminders
        if has(lower, #"\bremind(er| me)?\b|\bset a reminder\b"#) {
            i.intent = .createReminder
            var rest = text
            if let r = rest.range(of: #"(?i)^.*?\bremind(er)?( me)?( to| about| of)?\b"#, options: .regularExpression) { rest = String(rest[r.upperBound...]) }
            applyRecurrence(&i, lower)
            let d = dt.extractDate(from: rest)
            if let d {
                rest = d.rest
                // "every Monday" is a recurrence rule, not a one-off date
                if !(i.recurrence != nil && d.weekday != nil) { i.date = d.date }
                if i.weekday == nil { i.weekday = d.weekday }
            }
            if let t = dt.extractTime(from: rest) { i.time = t.time; rest = t.rest }
            i.title = cleanTitle(rest, strip: [#"\bevery\s+\w+\b"#, #"\b(daily|weekly|monthly|everyday)\b"#, #"\b(at|on|by|for)\s*$"#])
            if i.title?.isEmpty ?? true { i.title = "Reminder" }
            return i
        }

        // ---- calendar: delete / move / list / create
        if has(lower, #"\b(cancel|delete|remove)\b.*\b(meeting|event|appointment|call|review|standup|class|lecture|session)\b"#) {
            i.intent = .deleteEvent
            let d = dt.extractDate(from: text)
            i.date = d?.date
            i.query = eventQuery(from: d?.rest ?? text, removing: #"\b(cancel|delete|remove|my|the|a|an|that|this)\b"#)
            return i
        }
        if has(lower, #"\b(move|reschedule|shift|postpone|push)\b"#) {
            i.intent = .updateEvent
            // "move my AI project meeting to 5 PM"  /  "reschedule standup to friday at 3"
            var before = text, after = ""
            if let r = text.range(of: #"(?i)\b(to|until|till|for)\b"#, options: .regularExpression) {
                before = String(text[..<r.lowerBound]); after = String(text[r.upperBound...])
            }
            let dOld = dt.extractDate(from: before); if let dOld { i.date = dOld.date }
            let dNew = dt.extractDate(from: after); if let dNew { i.newDate = dNew.date }
            if let t = dt.extractTime(from: after) { i.newTime = t.time }
            i.query = eventQuery(from: dOld?.rest ?? before, removing: #"\b(move|reschedule|shift|postpone|push|my|the|a|an|that|this)\b"#)
            return i
        }
        // "schedule a meeting" / "schedule the call" must NOT be treated as a list query.
        // Only treat a leading "schedule/agenda/calendar" as a list intent when it stands alone
        // as a noun phrase (not followed by a direct object like "a meeting at 3").
        let scheduleAsNoun = has(lower, #"^(my\s+)?(schedule|agenda|calendar)\s*(\?|$)"#) ||
            has(lower, #"^(my\s+)?(schedule|agenda|calendar)\s+for\b"#)
        if has(lower, #"\b(what|which|show|list|do i have|any|tell me)\b.*\b(meetings?|events?|schedule|calendar|agenda|appointments?|classes|plans)\b"#) ||
            scheduleAsNoun || has(lower, #"\bwhat'?s (on )?(my )?(schedule|calendar|agenda|next)\b"#) {
            i.intent = (has(lower, #"\b(schedule|agenda|calendar)\b"#) && !has(lower, #"\bmeetings?\b"#)) ? .getSchedule : .listEvents
            if has(lower, #"\bnext\b"#) && has(lower, #"\b(meeting|event)\b"#) { i.intent = .listEvents; i.rangeDays = 7; i.query = "__next__" }
            let d = dt.extractDate(from: text)
            i.date = d?.date ?? dt.dateString(dt.now)
            if has(lower, #"\b(this week|week|upcoming|coming)\b"#) { i.rangeDays = 7 }
            if has(lower, #"\bnext 7 days\b"#) { i.rangeDays = 7 }
            return i
        }
        if has(lower, #"\b(schedule|set up|setup|book|add|create|put|plan|arrange)\b"#) &&
            (has(lower, #"\b(meeting|event|call|review|standup|stand-up|class|lecture|session|appointment|interview|presentation|demo|sync|study|exam|deadline|lunch|workshop)\b"#) || dt.extractTime(from: text) != nil || dt.extractDate(from: text) != nil) {
            i.intent = .createEvent
            var rest = text
            applyRecurrence(&i, lower)
            if let d = dt.extractDate(from: rest) { i.date = d.date; rest = d.rest; i.weekday = i.weekday ?? d.weekday }
            if let dur = dt.extractDuration(from: rest) { i.durationMinutes = dur; rest = rest.replacingOccurrences(of: #"(?i)\b(?:for\s+)?\d+(?:\.\d+)?\s*(hours?|hrs?|minutes?|mins?)\b"#, with: " ", options: .regularExpression) }
            if let t = dt.extractTime(from: rest) { i.time = t.time; rest = t.rest }
            // a "4 to 5 pm" style range
            i.title = cleanTitle(rest, strip: [#"^\s*(please\s+)?(can you\s+|could you\s+)?(schedule|set up|setup|book|add|create|put|plan|arrange)\b"#, #"\b(in|on|at|for|my|a|an|the)\s*$"#, #"^\s*(a|an|the|my)\s+"#, #"\b(in )?(my )?calendar\b"#])
            if i.title?.isEmpty ?? true { i.title = "Meeting" }
            if i.date == nil { i.date = dt.dateString(dt.now) }
            return i
        }
        return i   // general work query
    }

    // MARK: helpers
    private func applyRecurrence(_ i: inout Intent, _ lower: String) {
        if has(lower, #"\bevery\s+(week)?day\b|\bdaily\b|\beveryday\b"#) { i.recurrence = .daily }
        else if has(lower, #"\bevery\s+weekday\b|\bweekdays\b"#) { i.recurrence = .weekdays }
        else if has(lower, #"\bmonthly\b|\bevery\s+month\b"#) { i.recurrence = .monthly }
        else if has(lower, #"\bweekly\b|\bevery\s+week\b"#) { i.recurrence = .weekly }
        else {
            for (name, wd) in DateTimeParser.weekdayNames where has(lower, #"\bevery\s+"# + name + #"\b"#) {
                i.recurrence = .weekly; i.weekday = wd; break
            }
        }
    }

    private func eventQuery(from s: String, removing pattern: String) -> String? {
        var q = s.lowercased()
        q = q.replacingOccurrences(of: pattern, with: " ", options: .regularExpression)
        if let t = dt.extractTime(from: q) { q = t.rest }
        q = q.replacingOccurrences(of: #"\b(meeting|event|appointment)\b"#, with: " ", options: .regularExpression)
        q = q.replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression).trimmingCharacters(in: .whitespacesAndNewlines)
        return q.isEmpty ? nil : q
    }

    private func cleanTitle(_ s: String, strip patterns: [String]) -> String {
        var t = s
        for p in patterns { t = t.replacingOccurrences(of: p, with: " ", options: [.regularExpression, .caseInsensitive]) }
        t = t.replacingOccurrences(of: #"[\s,.;:]+"#, with: " ", options: .regularExpression).trimmingCharacters(in: .whitespacesAndNewlines)
        guard let f = t.first else { return "" }
        return String(f).uppercased() + t.dropFirst()
    }
}
