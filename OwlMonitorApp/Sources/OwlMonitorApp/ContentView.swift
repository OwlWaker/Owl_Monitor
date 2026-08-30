import SwiftUI

struct ContentView: View {
    enum Page: String, CaseIterable, Identifiable {
        case performance = "性能"
        case processes = "进程"
        case settings = "设置"
        var id: String { rawValue }
    }
    @State private var page: Page = .performance

    var body: some View {
        pageContent
            .frame(minWidth: 900, minHeight: 620)
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Picker("页面", selection: $page) {
                        ForEach(Page.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 260)
                }
            }
            .toolbarBackground(.clear, for: .windowToolbar)
            .background(WindowAccessor())
    }

    @ViewBuilder
    var pageContent: some View {
        switch page {
        case .performance: PerformanceView()
        case .processes: ProcessesView()
        case .settings: SettingsView()
        }
    }
}
