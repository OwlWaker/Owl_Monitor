import Foundation
import Combine

/// 用户可调设置，持久化到 `UserDefaults`（重启后保留）。
final class AppSettings: ObservableObject {
    @Published var perfRefreshInterval: Double {
        didSet { persist() }
    }
    @Published var processRefreshInterval: Double {
        didSet { persist() }
    }
    @Published var historyWindow: Int {
        didSet { persist() }
    }
    @Published var killWaitSeconds: Double {
        didSet { persist() }
    }

    private enum Key {
        static let perf = "settings.perfRefreshInterval"
        static let proc = "settings.processRefreshInterval"
        static let window = "settings.historyWindow"
        static let kill = "settings.killWaitSeconds"
    }

    init() {
        let d = UserDefaults.standard
        perfRefreshInterval = d.object(forKey: Key.perf) == nil ? 1.0 : d.double(forKey: Key.perf)
        processRefreshInterval = d.object(forKey: Key.proc) == nil ? 1.0 : d.double(forKey: Key.proc)
        historyWindow = d.object(forKey: Key.window) == nil ? 60 : d.integer(forKey: Key.window)
        killWaitSeconds = d.object(forKey: Key.kill) == nil ? 2.0 : d.double(forKey: Key.kill)
    }

    private func persist() {
        let d = UserDefaults.standard
        d.set(perfRefreshInterval, forKey: Key.perf)
        d.set(processRefreshInterval, forKey: Key.proc)
        d.set(historyWindow, forKey: Key.window)
        d.set(killWaitSeconds, forKey: Key.kill)
    }
}
