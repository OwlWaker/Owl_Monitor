import AppKit
import Darwin
import SwiftUI

/// 进程页：按「应用 / 后台进程」分组，显示 PID / CPU / 内存，支持搜索与结束进程。
struct ProcessesView: View {
    @State private var search = ""
    @State private var selectedPid: pid_t?
    @State private var pendingKill: pid_t?
    @State private var showConfirm = false
    @State private var showForce = false
    @EnvironmentObject private var model: MonitorModel
    @EnvironmentObject private var settings: AppSettings

    var body: some View {
        NavigationStack {
            List(selection: $selectedPid) {
                Section {
                    ForEach(grouped["应用"] ?? []) { p in
                        row(p)
                    }
                } header: {
                    ProcessSectionHeader(title: "应用", count: grouped["应用"]?.count ?? 0)
                }

                Section {
                    ForEach(grouped["后台进程"] ?? []) { p in
                        row(p)
                    }
                } header: {
                    ProcessSectionHeader(title: "后台进程", count: grouped["后台进程"]?.count ?? 0)
                }
            }
            .listStyle(.plain)
            .searchable(text: $search)
            .navigationTitle("进程")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button("结束进程") {
                        if let pid = selectedPid {
                            pendingKill = pid
                            showConfirm = true
                        }
                    }
                    .buttonStyle(.glass)
                    .tint(.red)
                    .disabled(selectedPid == nil)
                }
            }
        }
        .alert("结束进程", isPresented: $showConfirm) {
            Button("取消", role: .cancel) { pendingKill = nil }
            Button("结束", role: .destructive) {
                if let pid = pendingKill { terminate(pid) }
            }
        } message: {
            Text(confirmMessage)
        }
        .alert("强制结束", isPresented: $showForce) {
            Button("取消", role: .cancel) { pendingKill = nil }
            Button("强制结束", role: .destructive) {
                if let pid = pendingKill { kill(pid, SIGKILL) }
                pendingKill = nil
            }
        } message: {
            Text("进程未响应，是否强制结束？")
        }
    }

    private func row(_ p: ProcessItem) -> some View {
        HStack(spacing: ProcessColumns.spacing) {
            HStack(spacing: ProcessColumns.iconSpacing) {
                Image(nsImage: p.icon ?? NSImage())
                    .resizable()
                    .frame(width: ProcessColumns.iconSize, height: ProcessColumns.iconSize)
                Text(p.name).lineLimit(1)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            Text(String(p.id))
                .frame(width: ProcessColumns.pid, alignment: .trailing)
            Text(String(format: "%.1f%%", p.cpuPercent))
                .frame(width: ProcessColumns.cpu, alignment: .trailing)
            Text(fmtBytes(p.memBytes))
                .frame(width: ProcessColumns.mem, alignment: .trailing)
        }
        .font(.callout)
        .listRowInsets(EdgeInsets(top: 0, leading: ProcessColumns.rowIndent, bottom: 0, trailing: 0))
        .tag(p.id)
    }

    private var grouped: [String: [ProcessItem]] {
        var dict: [String: [ProcessItem]] = ["应用": [], "后台进程": []]
        for p in filteredList {
            let key = p.isApp ? "应用" : "后台进程"
            dict[key, default: []].append(p)
        }
        return dict
    }

    private var filteredList: [ProcessItem] {
        search.isEmpty ? model.processes : model.processes.filter { $0.name.localizedCaseInsensitiveContains(search) }
    }

    private var confirmMessage: String {
        guard let pid = pendingKill else { return "" }
        let name = model.processes.first { $0.id == pid }?.name ?? "进程"
        return "确定要结束进程「\(name)」吗？"
    }

    /// 结束进程：先发 SIGTERM，等待后若仍存活则弹窗请用户强制结束。
    private func terminate(_ pid: pid_t) {
        kill(pid, SIGTERM)
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: UInt64(settings.killWaitSeconds * 1_000_000_000))
            if processAlive(pid) {
                showForce = true
            } else {
                pendingKill = nil
            }
        }
    }

    private func processAlive(_ pid: pid_t) -> Bool {
        if kill(pid, 0) == 0 { return true }
        return errno == EPERM
    }
}
