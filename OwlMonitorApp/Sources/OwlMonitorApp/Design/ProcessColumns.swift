import CoreGraphics

/// 进程列表的列宽与间距，集中定义避免魔法数字散落各处。
enum ProcessColumns {
    static let pid: CGFloat = 70
    static let cpu: CGFloat = 70
    static let mem: CGFloat = 90
    static let spacing: CGFloat = 12
    static let iconSpacing: CGFloat = 8
    static let iconSize: CGFloat = 24
    /// 子列表行向右缩进量（相对分组表头）。
    static let rowIndent: CGFloat = 16
}
