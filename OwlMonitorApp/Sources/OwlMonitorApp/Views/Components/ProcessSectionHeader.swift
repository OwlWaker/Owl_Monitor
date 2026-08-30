import SwiftUI

/// 「应用」/「后台进程」分组表头：左侧显示数量与名称，右侧并入列名。
/// 作为 `List` 的 `Section` 表头使用，滚动时吸顶。
struct ProcessSectionHeader: View {
    let title: String
    let count: Int

    var body: some View {
        HStack(spacing: ProcessColumns.spacing) {
            HStack(spacing: ProcessColumns.iconSpacing) {
                Text("\(title) (\(count))")
                Text("名称")
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            Text("PID").frame(width: ProcessColumns.pid, alignment: .trailing)
            Text("CPU").frame(width: ProcessColumns.cpu, alignment: .trailing)
            Text("内存").frame(width: ProcessColumns.mem, alignment: .trailing)
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .padding(.vertical, 6)
        .frame(maxWidth: .infinity)
    }
}
