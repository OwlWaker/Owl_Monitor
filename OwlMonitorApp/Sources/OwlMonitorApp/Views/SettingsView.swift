import SwiftUI

/// 设置页：让用户调节刷新间隔、历史窗口、结束进程等待时间。
struct SettingsView: View {
    @EnvironmentObject private var settings: AppSettings

    var body: some View {
        Form {
            Section("刷新间隔") {
                Stepper(value: $settings.perfRefreshInterval, in: 0.5...5.0, step: 0.5) {
                    Text("性能页刷新：\(settings.perfRefreshInterval, specifier: "%.1f") 秒")
                }
                Stepper(value: $settings.processRefreshInterval, in: 0.5...5.0, step: 0.5) {
                    Text("进程页刷新：\(settings.processRefreshInterval, specifier: "%.1f") 秒")
                }
            }
            Section("图表") {
                Stepper(value: $settings.historyWindow, in: 20...180, step: 10) {
                    Text("历史窗口：\(settings.historyWindow) 个采样点")
                }
            }
            Section("结束进程") {
                Stepper(value: $settings.killWaitSeconds, in: 0.5...10.0, step: 0.5) {
                    Text("检测等待：\(settings.killWaitSeconds, specifier: "%.1f") 秒")
                }
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}
