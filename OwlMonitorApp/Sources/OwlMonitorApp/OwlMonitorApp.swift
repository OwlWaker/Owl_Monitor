import AppKit
import SwiftUI

@main
struct OwlMonitorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var settings: AppSettings
    @StateObject private var model: MonitorModel

    init() {
        // 用命令行 (swift run) 运行时不是 .app bundle，默认不激活，窗口不会前置。
        // 设为 regular 并激活，让窗口正常显示。
        NSApplication.shared.setActivationPolicy(.regular)
        NSApplication.shared.activate(ignoringOtherApps: true)

        let s = AppSettings()
        _settings = StateObject(wrappedValue: s)
        _model = StateObject(wrappedValue: MonitorModel(settings: s))
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(settings)
                .environmentObject(model)
        }
    }
}

/// 应用代理：关闭最后一个窗口后直接退出，避免程序残留在后台。
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }
}
