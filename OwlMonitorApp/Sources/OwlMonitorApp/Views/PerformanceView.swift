import SwiftUI

/// 性能页：仿 Windows 任务管理器，左侧类别侧栏 + 右侧大走势图与统计卡片。
struct PerformanceView: View {
    @State private var selection: PerformanceCategory = .cpu
    @EnvironmentObject private var model: MonitorModel
    @EnvironmentObject private var settings: AppSettings

    var body: some View {
        HStack(spacing: 0) {
            sidebar
                .frame(width: 220)
            Divider()
            mainContent
        }
    }

    // MARK: - 左侧类别侧栏

    private var sidebar: some View {
        List(selection: $selection) {
            ForEach(PerformanceCategory.allCases) { cat in
                HStack(spacing: 12) {
                    Image(systemName: cat.symbol)
                        .foregroundStyle(cat.accent)
                        .imageScale(.large)
                        .frame(width: 24)
                    Text(cat.rawValue)
                    Spacer(minLength: 4)
                    Sparkline(data: model.perfHistories[cat] ?? [], color: cat.accent, windowSize: settings.historyWindow, maxValue: maxValue(for: cat))
                        .frame(width: 48, height: 22)
                }
                .tag(cat)
            }
        }
.listStyle(.plain)
    }

    // MARK: - 右侧主区域

    private var mainContent: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(selection.rawValue)
                .font(.title2.bold())

            UsageChart(data: model.perfHistories[selection] ?? [], color: selection.accent, windowSize: settings.historyWindow, maxValue: maxValue(for: selection))
                .frame(height: 200)
                .background(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius).fill(.ultraThinMaterial))
                .clipShape(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius))

            statsGrid

            Spacer(minLength: 0)
        }
        .padding(20)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    private var statsGrid: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 160), spacing: AppMetrics.contentSpacing)], spacing: AppMetrics.contentSpacing) {
            ForEach(stats) { stat in
                statCard(stat)
            }
        }
    }

    private var stats: [PerfStat] {
        switch selection {
        case .cpu:
            return [
                PerfStat(title: "利用率", value: percent(current(for: .cpu))),
                PerfStat(title: "逻辑处理器", value: "\(ProcessInfo.processInfo.activeProcessorCount)"),
            ]
        case .memory:
            let total = model.memTotal
            let used = model.memUsed
            return [
                PerfStat(title: "已使用", value: fmtBytes(used)),
                PerfStat(title: "可用", value: fmtBytes(total > used ? total - used : 0)),
                PerfStat(title: "总计", value: fmtBytes(total)),
            ]
        case .disk:
            return [
                PerfStat(title: "读取", value: mbps(model.diskReadMBps)),
                PerfStat(title: "写入", value: mbps(model.diskWriteMBps)),
            ]
        case .network:
            return [PerfStat(title: "网络", value: percent(current(for: .network)))]
        case .gpu:
            return [PerfStat(title: "GPU 使用率", value: percent(current(for: .gpu)))]
        }
    }

    private func statCard(_ s: PerfStat) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(s.title).font(.caption).foregroundStyle(.secondary)
            Text(s.value).font(.title3.bold())
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(AppMetrics.cardPadding)
        .background(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius).fill(.ultraThinMaterial))
        .overlay(RoundedRectangle(cornerRadius: AppMetrics.cardCornerRadius).stroke(.white.opacity(AppMetrics.strokeOpacity), lineWidth: 1))
    }

    // MARK: - 采样

    private func current(for cat: PerformanceCategory) -> Double {
        model.perfHistories[cat]?.last ?? 0
    }

    private func percent(_ v: Double) -> String {
        String(format: "%.0f%%", v)
    }

    private func mbps(_ v: Double) -> String {
        String(format: "%.1f MB/s", v)
    }

    /// 磁盘是吞吐量（非百分比），图内按最大值归一化；其余类别固定 0-100 刻度。
    private func maxValue(for cat: PerformanceCategory) -> Double? {
        cat == .disk ? nil : 100
    }

}

/// 统计卡片的数据模型。
private struct PerfStat: Identifiable {
    let id = UUID()
    let title: String
    let value: String
}
