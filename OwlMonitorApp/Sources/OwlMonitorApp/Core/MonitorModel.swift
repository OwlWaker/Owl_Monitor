import Foundation
import Combine

/// 应用级监控模型：持续采集性能与进程数据，与当前显示的页面无关。
@MainActor
final class MonitorModel: ObservableObject {
    @Published var perfHistories: [PerformanceCategory: [Double]] = [:]
    @Published var memUsed: UInt64 = 0
    @Published var diskReadMBps: Double = 0
    @Published var diskWriteMBps: Double = 0
    @Published var processes: [ProcessItem] = []

    let memTotal: UInt64

    private let perf = PerformanceSampler()
    private let proc = ProcessSampler()
    private let settings: AppSettings
    private var timer: Timer?
    private var lastPerf: Double = 0
    private var lastProc: Double = 0
    private let baseInterval: TimeInterval = 0.5

    init(settings: AppSettings) {
        self.settings = settings
        self.memTotal = perf.memTotal
        perf.historyWindow = settings.historyWindow
        start()
    }

    /// 开始持续采集（应用启动即运行，与页面无关）。
    func start() {
        stop()
        lastPerf = 0
        lastProc = 0
        timer = Timer.scheduledTimer(withTimeInterval: baseInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.tick()
            }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        let now = ProcessInfo.processInfo.systemUptime
        perf.historyWindow = settings.historyWindow

        if now - lastPerf >= settings.perfRefreshInterval {
            perf.sample()
            perfHistories = perf.histories
            memUsed = perf.memUsed
            diskReadMBps = perf.diskReadMBps
            diskWriteMBps = perf.diskWriteMBps
            lastPerf = now
        }

        if now - lastProc >= settings.processRefreshInterval {
            processes = proc.sample()
            lastProc = now
        }
    }
}
