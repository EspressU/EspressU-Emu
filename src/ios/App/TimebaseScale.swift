import Foundation

enum TimebaseScale: Int, CaseIterable, Identifiable {
    case realTime = 3
    case half = 4
    case quarter = 5
    case eighth = 6
    case sixteenth = 7
    case thirtySecond = 8
    case sixtyFourth = 9

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .realTime:     return "Real time"
        case .half:         return "1/2 speed"
        case .quarter:      return "1/4 speed"
        case .eighth:       return "1/8 speed"
        case .sixteenth:    return "1/16 speed"
        case .thirtySecond: return "1/32 speed"
        case .sixtyFourth:  return "1/64 speed"
        }
    }

    var summary: String {
        switch self {
        case .realTime:
            return "Normal game speed. Right when the recompiler is running."
        case .half, .quarter:
            return "Slows the game a little. Try this first if a game runs but stutters."
        case .eighth:
            return "Slows the game a lot. Helps most games under the interpreter."
        case .sixteenth, .thirtySecond:
            return "For games that still won't advance at 1/8. Gameplay runs in slow motion."
        case .sixtyFourth:
            return "The slowest setting. If a game still won't move, the clock isn't the problem."
        }
    }

    static let storageKey = "timebaseShift"

    static var hasExplicitChoice: Bool {
        UserDefaults.standard.object(forKey: storageKey) != nil
    }

    static var current: TimebaseScale {
        if hasExplicitChoice,
           let value = TimebaseScale(rawValue: UserDefaults.standard.integer(forKey: storageKey)) {
            return value
        }
        return TimebaseScale(rawValue: Int(cemu_bridge_get_timebase_shift())) ?? .realTime
    }

    static func apply(_ scale: TimebaseScale) {
        UserDefaults.standard.set(scale.rawValue, forKey: storageKey)
        cemu_bridge_set_timebase_auto_enabled(false)
        cemu_bridge_set_timebase_shift(Int32(scale.rawValue))
    }

    static func clearChoice() {
        UserDefaults.standard.removeObject(forKey: storageKey)
        cemu_bridge_set_timebase_shift(Int32(TimebaseScale.realTime.rawValue))
        cemu_bridge_set_timebase_auto_enabled(true)
    }

    private static let repairedKey = "espressu.timebase.clearedAccidentalChoice"

    static func repairAccidentalChoiceOnce() {
        guard !UserDefaults.standard.bool(forKey: repairedKey) else { return }
        UserDefaults.standard.set(true, forKey: repairedKey)
        guard hasExplicitChoice,
              let stored = TimebaseScale(rawValue: UserDefaults.standard.integer(forKey: storageKey)),
              stored != .realTime
        else { return }
        UserDefaults.standard.removeObject(forKey: storageKey)
        cemu_bridge_log_line("timebase: cleared a stored \(stored.title) clock that the app had written to itself; back to automatic")
    }

    static func applyStoredChoiceIfAny() {
        repairAccidentalChoiceOnce()
        guard hasExplicitChoice,
              let value = TimebaseScale(rawValue: UserDefaults.standard.integer(forKey: storageKey))
        else {
            cemu_bridge_set_timebase_auto_enabled(true)
            return
        }
        cemu_bridge_set_timebase_auto_enabled(false)
        cemu_bridge_set_timebase_shift(Int32(value.rawValue))
    }
}
