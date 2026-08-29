#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 系统信息模块：采集 macOS 的系统总体信息与进程列表，供任务管理器展示。
// 只依赖系统 API（mach / libproc / sysctl），不引入第三方库。
// System info module: samples overall system stats and the process list for the
// task manager. Uses only system APIs (mach / libproc / sysctl).
namespace sys {

// 系统总体信息（概览页展示的数据）
struct Overview {
    double cpu_total = 0.0;   // 总 CPU 使用率（%，0-100）
    double mem_used = 0.0;    // 已使用内存（字节，活动监视器口径 = App+联动+被压缩）
    double mem_total = 0.0;   // 物理内存总量（字节）
    double mem_percent = 0.0; // 内存占用率（%）
    double mem_app = 0.0;        // App 内存：应用匿名页（可被压缩/换出）
    double mem_wired = 0.0;      // 联动内存：内核/驱动占用，不可换出
    double mem_compressed = 0.0; // 被压缩的内存（内存压缩器占用的页）
    double mem_cached = 0.0;     // 已缓存文件：文件缓存页（可回收）
    double disk_read_bs = 0.0;   // 磁盘读速率（字节/秒，系统级）
    double disk_write_bs = 0.0;  // 磁盘写速率（字节/秒，系统级）
    double disk_total = 0.0;     // 磁盘总容量（字节，主卷）
    double disk_free = 0.0;      // 磁盘可用空间（字节，主卷）
    int    proc_count = 0;    // 进程总数
};

// 单个进程的信息
struct ProcInfo {
    int    pid = 0;           // 进程 ID
    std::string name;         // 进程名
    double cpu_percent = 0.0; // CPU 使用率（%，基于最近一次采样间隔的差值）
    double mem_bytes = 0.0;   // 常驻物理内存（字节）
    double mem_percent = 0.0; // 占总内存比例（%）
    int    threads = 0;       // 线程数
    std::string status;       // 运行状态描述（运行/睡眠/停止/僵尸等）
    double disk_read_bs = 0.0;   // 磁盘读速率（字节/秒）
    double disk_write_bs = 0.0;  // 磁盘写速率（字节/秒）
    bool   is_app = false;    // 是否拥有窗口的"应用"（CGWindow 实测；用于图标颜色与分组）
    bool   frontmost = false; // 是否当前前台（激活）应用（NSWorkspace 实测）
};

// 系统界面语言是否为中文（基于 macOS 首选语言 AppleLanguages，zh 开头即中文）
// Whether the system UI language is Chinese (macOS preferred language starts with zh)
bool system_language_zh();

// 翻译帮助：系统语言为中文时返回 zh，否则返回 en
// Translation helper: return zh when the system language is Chinese, else en
inline const char* tr(const char* zh, const char* en) {
    return system_language_zh() ? zh : en;
}

// 采样器：维护上一次 CPU 时间快照，用于计算两次采样之间（相对平滑）的 CPU 使用率。
// 每次 init/sample 都会推进快照，因此应周期性调用。
// Sampler: keeps the previous CPU time snapshot so CPU usage is computed from the
// delta between two samples instead of a single instantaneous read.
class Sampler {
public:
    // 初始化并预热快照（计算使用率需要至少两次采样）
    // Initialize and warm up the snapshot (usage needs at least two samples)
    bool init();

    // 采样系统总体信息（CPU 使用率、内存、进程数）
    // Sample overall system information
    bool sample_overview(Overview& out);

    // 采样每逻辑核的 CPU 使用率（%），out.size() 与逻辑核数一致。
    // 与 sample_overview 一样基于两次采样差值，首次调用返回全 0。
    // Sample per-logical-core CPU usage (%), size equals the logical core count.
    bool sample_cores(std::vector<double>& out);

    // 采样进程列表（名称、PID、CPU%、内存）
    // Sample the process list
    bool sample_procs(std::vector<ProcInfo>& out);

private:
    // 上次采样时刻（秒）
    double prev_time_ = 0;
    // 上次进程列表采样时刻（秒）：进程 CPU% 用独立时间戳，避免与 sample_overview 共用
    // 的 prev_time_ 在同一帧被重置，导致 dt≈0 而把 CPU% 放大到上千。
    double prev_proc_time_ = 0;
    // 上次总 CPU 与空闲 CPU 时间（tick）
    uint64_t prev_total_ = 0;
    uint64_t prev_idle_ = 0;
    // 每个逻辑核上次的 (总, 空闲) 时间，用于计算每核使用率
    struct CoreTick { uint64_t total = 0, idle = 0; };
    std::vector<CoreTick> prev_cores_;
    // 每个进程上次的累计 CPU 时间（用于计算进程 CPU 使用率）
    struct ProcTick { int pid; uint64_t cpu; };
    std::vector<ProcTick> prev_proc_;
    // 每个进程上次的累计磁盘读写字节（用于计算磁盘速率）
    struct ProcDisk { int pid; uint64_t read = 0, write = 0; };
    std::vector<ProcDisk> prev_disk_;
    // 系统级磁盘累计读写字节（用于计算总磁盘速率）
    uint64_t prev_disk_read_ = 0, prev_disk_write_ = 0;
    double prev_disk_time_ = 0;
};

// 各级缓存大小（字节），Apple 芯片 L1 分指令/数据
// Cache sizes in bytes; L1 is split into instruction/data on Apple chips
struct CacheInfo {
    double l1i = 0;  // L1 指令缓存
    double l1d = 0;  // L1 数据缓存
    double l2 = 0;   // L2 缓存（P/E 核共享的二级缓存）
    double l3 = 0;   // L3 缓存（系统级，可能为 0）
};

// CPU 硬件信息（一次性读取，硬件不变化）
struct CpuInfo {
    std::string model;   // 芯片型号，如 "Apple M5"
    int s_cores = 0;     // 超级核心（Super / S）数量（hw.perflevelN.name == "Super"）
    int p_cores = 0;     // 性能核心（Performance / P）数量（name == "Performance"）
    int e_cores = 0;     // 能效核心（Efficiency / E）数量（name == "Efficiency"）
    int physical = 0;    // 物理核总数
    int logical = 0;     // 逻辑核总数（含超线程）
    CacheInfo cache;     // 各级缓存
};

// GPU 硬件信息（一次性读取）
struct GpuInfo {
    std::string name;      // GPU 名称，如 "Apple M3" / "AMD Radeon Pro ..."
    std::string arch;      // 架构描述，如 "Apple GPU（集成）" / "NVIDIA"
    int cores = 0;         // GPU 核心数（0 表示无法获取）
    double mem_bytes = 0;  // 显存 / 统一内存（字节）
};

// 读取当前前台（激活）应用的 PID；失败返回 -1
// Get the PID of the current frontmost (active) app, or -1 on failure
int foreground_pid();

// 返回所有"拥有普通应用窗口"的进程 PID 集合（含非焦点、最小化）；实现在 frontmost.mm
// Return the PIDs of every process that owns a normal app window (incl. minimized)
std::vector<int> app_window_pids();

// 读取 pid 对应应用图标的 RGBA8 打包位图（R=低8位..A=高8位）；返回像素指针与尺寸。
// 图标按固定尺寸栅格化并按 pid 缓存。无图标返回 false。
// Read the app icon for a pid as RGBA8-packed pixels (R=low..A=high); cached by pid.
bool app_icon_rgba(int pid, const uint32_t*& px, int& w, int& h);

// 读取 CPU 硬件信息（核心、型号、缓存），返回是否成功
// Read CPU hardware info (cores, model, cache)
bool sample_cpu_info(CpuInfo& out);

// 读取 GPU 硬件信息（名称、架构、核心数、显存），返回是否成功
// Read GPU hardware info (name, arch, cores, memory)
bool sample_gpu_info(GpuInfo& out);

// 读取 GPU 总体使用率（%，0-100），无法获取时返回 -1。
// Apple 集成 GPU 来自 IOKit 的 PerformanceStatistics 统计；独立 GPU 可能不可用。
// Read the overall GPU utilization (%), or -1 if unavailable.
double sample_gpu_utilization();

// 请求终止进程：sig 为 SIGTERM（正常退出）或 SIGKILL（强制结束）。
// 返回 0 表示信号发送成功；-1 表示失败并把 errno 写入 out_errno
// （EPERM/EACCES 表示权限不足，ESRCH 表示无此进程）。
// Request to terminate a process with the given signal (SIGTERM / SIGKILL).
// Returns 0 on success; -1 on failure with errno written to out_errno.
int terminate_process(int pid, int sig, int* out_errno = nullptr);

// 探测进程是否仍存活（kill(pid, 0)，不发送信号）。
// Probe whether the process is still alive (kill(pid, 0) sends no signal).
bool process_exists(int pid);

}
