#include "sys/system.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <unistd.h>

#include <errno.h>
#include <signal.h>

#include <mach/mach.h>
#include <libproc.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

namespace sys {
namespace {

// 当前时间（秒），用于采样间隔计算
// Current time in seconds for sample intervals
double now_seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// 读取系统总 CPU 时间与空闲时间（tick 数）
// Read the total and idle CPU ticks
void cpu_total_idle(uint64_t& total, uint64_t& idle) {
    host_cpu_load_info_data_t info{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&info, &count) == KERN_SUCCESS) {
        total = (uint64_t)info.cpu_ticks[CPU_STATE_USER] + (uint64_t)info.cpu_ticks[CPU_STATE_SYSTEM] +
                (uint64_t)info.cpu_ticks[CPU_STATE_IDLE] + (uint64_t)info.cpu_ticks[CPU_STATE_NICE];
        idle  = (uint64_t)info.cpu_ticks[CPU_STATE_IDLE];
    } else {
        total = idle = 0;
    }
}

// 读取物理内存分类统计（字节）：App / 联动 / 被压缩 / 已缓存文件、已使用与总量。
// 分类与 macOS 活动监视器口径一致：App ≈ 内部匿名页 − 可清除页；联动 = wire；
// 被压缩 = compressor；已缓存 = external（文件缓存）；已使用 = App + 联动 + 被压缩。
// Read the memory breakdown matching Activity Monitor: app/wired/compressed/cached.
void memory_stats(double& app, double& wired, double& compressed, double& cached,
                  double& used, double& total) {
    const long page_size  = sysconf(_SC_PAGESIZE);
    const long phys_pages = sysconf(_SC_PHYS_PAGES);
    total = (double)phys_pages * page_size;

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vm, &count) == KERN_SUCCESS) {
        wired      = (double)vm.wire_count * page_size;
        compressed = (double)vm.compressor_page_count * page_size;
        cached     = (double)vm.external_page_count * page_size;   // 文件缓存页
        // App 内存 ≈ 内部匿名页减去可清除页
        const double internal  = (double)vm.internal_page_count * page_size;
        const double purgeable = (double)vm.purgeable_count * page_size;
        app = internal > purgeable ? internal - purgeable : 0;
    }
    // 已使用内存（活动监视器口径）= App + 联动 + 被压缩
    used = app + wired + compressed;
    if (used > total) used = total;
}

// 把 BSD 进程状态码映射为可读文本（运行/睡眠/停止/僵尸等）
// Map a BSD process status code to readable text
std::string proc_status_str(int stat) {
    switch (stat) {
        case SIDL:   return sys::tr("空闲", "Idle");
        case SRUN:   return sys::tr("运行", "Running");
        case SSLEEP: return sys::tr("睡眠", "Sleeping");
        case SSTOP:  return sys::tr("停止", "Stopped");
        case SZOMB:  return sys::tr("僵尸", "Zombie");
        default:     return "";
    }
}

// 单个进程名（对权限不足的进程可能为空）
// Process name (may be empty for processes without read permission)
std::string proc_name_str(int pid) {
    char buf[PROC_PIDPATHINFO_MAXSIZE] = {0};
    const int n = proc_name(pid, buf, sizeof(buf));
    return n > 0 ? std::string(buf) : std::string();
}

// 读取系统级磁盘累计读写字节（IOBlockStorageDriver 的 Statistics 累计值）
// Read the system-wide cumulative disk read/write bytes from IOKit.
bool system_disk_bytes(uint64_t& read, uint64_t& write) {
    read = write = 0;
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOBlockStorageDriver"), &iter) != KERN_SUCCESS)
        return false;
    io_object_t dev;
    bool ok = false;
    while ((dev = IOIteratorNext(iter))) {
        CFMutableDictionaryRef props = nullptr;
        if (IORegistryEntryCreateCFProperties(dev, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
            CFDictionaryRef stats = (CFDictionaryRef)CFDictionaryGetValue(props, CFSTR("Statistics"));
            if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
                CFNumberRef rv = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("Bytes (Read)"));
                CFNumberRef wv = (CFNumberRef)CFDictionaryGetValue(stats, CFSTR("Bytes (Written)"));
                long long rr = 0, ww = 0;
                if (rv && CFGetTypeID(rv) == CFNumberGetTypeID()) CFNumberGetValue(rv, kCFNumberLongLongType, &rr);
                if (wv && CFGetTypeID(wv) == CFNumberGetTypeID()) CFNumberGetValue(wv, kCFNumberLongLongType, &ww);
                read += (uint64_t)rr; write += (uint64_t)ww; ok = true;
            }
            CFRelease(props);
        }
        IOObjectRelease(dev);
    }
    IOObjectRelease(iter);
    return ok;
}

} // namespace

bool Sampler::init() {
    cpu_total_idle(prev_total_, prev_idle_);
    std::vector<double> warm;
    sample_cores(warm);  // 预热每核快照，使首次真实采样即有有效差值
    prev_time_ = now_seconds();
    prev_proc_time_ = now_seconds();  // 预热进程采样时间戳
    // 预热系统磁盘累计字节，使首次磁盘采样有有效差值
    system_disk_bytes(prev_disk_read_, prev_disk_write_);
    prev_disk_time_ = now_seconds();
    return true;
}

bool Sampler::sample_overview(Overview& out) {
    // ---- CPU 使用率：两次采样的 tick 差值 ----
    uint64_t total = 0, idle = 0;
    cpu_total_idle(total, idle);
    const double now = now_seconds();
    const double dt = now - prev_time_;
    if (dt > 0.001 && total > prev_total_) {
        const uint64_t d_total = total - prev_total_;
        const uint64_t d_idle  = idle - prev_idle_;
        int64_t busy = (int64_t)(d_total - d_idle);  // 空闲可能因统计误差略超总时间，钳制非负
        if (busy < 0) busy = 0;
        out.cpu_total = (double)busy / (double)d_total * 100.0;
        if (out.cpu_total < 0) out.cpu_total = 0;
        if (out.cpu_total > 100) out.cpu_total = 100;
    } else {
        out.cpu_total = 0;
    }
    prev_total_ = total; prev_idle_ = idle; prev_time_ = now;

    // ---- 内存分类统计（活动监视器口径）----
    double app = 0, wired = 0, compressed = 0, cached = 0, used = 0, mem_total = 0;
    memory_stats(app, wired, compressed, cached, used, mem_total);
    out.mem_app = app;
    out.mem_wired = wired;
    out.mem_compressed = compressed;
    out.mem_cached = cached;
    out.mem_used = used;
    out.mem_total = mem_total;
    out.mem_percent = mem_total > 0 ? used / mem_total * 100.0 : 0;

    // ---- 进程数 ----
    const int count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    out.proc_count = count > 0 ? count : 0;

    // ---- 磁盘速率（系统级，IOBlockStorageDriver 累计字节差值）----
    uint64_t dr = 0, dw = 0;
    if (system_disk_bytes(dr, dw)) {
        const double ddt = now - prev_disk_time_;
        if (ddt > 0.001) {
            out.disk_read_bs = (double)(dr - prev_disk_read_) / ddt;
            out.disk_write_bs = (double)(dw - prev_disk_write_) / ddt;
            if (out.disk_read_bs < 0) out.disk_read_bs = 0;
            if (out.disk_write_bs < 0) out.disk_write_bs = 0;
        }
        prev_disk_read_ = dr; prev_disk_write_ = dw; prev_disk_time_ = now;
    }
    // 磁盘容量（主卷）
    {
        struct statfs fs{};
        if (statfs("/", &fs) == 0) {
            out.disk_total = (double)fs.f_blocks * (double)fs.f_bsize;
            out.disk_free  = (double)fs.f_bavail * (double)fs.f_bsize;
        }
    }
    return true;
}

bool Sampler::sample_cores(std::vector<double>& out) {
    // 用 host_processor_info 获取每个逻辑处理器的 ticks，再按两次采样差值计算
    // Per-logical-core CPU usage from host_processor_info, using the sample delta
    natural_t cpu_count = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t msg_count = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                            &cpu_count, &info, &msg_count) != KERN_SUCCESS) return false;
    auto* loads = (processor_cpu_load_info_t)info;

    out.clear();
    out.reserve(cpu_count);
    for (natural_t i = 0; i < cpu_count; ++i) {
        const uint64_t total = (uint64_t)loads[i].cpu_ticks[CPU_STATE_USER] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_SYSTEM] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_IDLE] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_NICE];
        const uint64_t idle = (uint64_t)loads[i].cpu_ticks[CPU_STATE_IDLE];
        double use = 0;
        if (i < prev_cores_.size() && total > prev_cores_[i].total) {
            const uint64_t d_total = total - prev_cores_[i].total;
            const uint64_t d_idle = idle - prev_cores_[i].idle;
            if (d_total > 0) {
                int64_t busy = (int64_t)(d_total - d_idle);  // 空闲可能略超总时间，钳制非负
                if (busy < 0) busy = 0;
                use = (double)busy / (double)d_total * 100.0;
            }
            if (use < 0) use = 0; else if (use > 100) use = 100;
        }
        out.push_back(use);
    }
    // 保存本次快照供下次计算
    prev_cores_.resize(cpu_count);
    for (natural_t i = 0; i < cpu_count; ++i) {
        prev_cores_[i].total = (uint64_t)loads[i].cpu_ticks[CPU_STATE_USER] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_SYSTEM] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_IDLE] +
                               (uint64_t)loads[i].cpu_ticks[CPU_STATE_NICE];
        prev_cores_[i].idle = (uint64_t)loads[i].cpu_ticks[CPU_STATE_IDLE];
    }
    vm_deallocate(mach_task_self(), (vm_address_t)info, msg_count * sizeof(integer_t));
    return true;
}

bool Sampler::sample_procs(std::vector<ProcInfo>& out) {
    // 枚举所有 PID
    // Enumerate all PIDs
    const int count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (count <= 0) return false;
    // 注意：proc_listpids 返回的是字节数，真正的 PID 个数 = count / sizeof(pid_t)
    const int pid_count = count / (int)sizeof(pid_t);
    const int front = foreground_pid();  // 当前前台应用 PID（用于 frontmost 标记；实现在 frontmost.mm）
    // 拥有普通应用窗口的 PID 集合（含非焦点、最小化），用于 is_app 判定与分组
    // PIDs owning a normal app window (incl. non-frontmost / minimized)
    const std::vector<int> app_pids = app_window_pids();
    const std::unordered_set<int> app_set(app_pids.begin(), app_pids.end());
    std::vector<pid_t> pids((size_t)pid_count);
    proc_listpids(PROC_ALL_PIDS, 0, pids.data(), (int)(pids.size() * sizeof(pid_t)));

    const double now = now_seconds();
    const double dt = now - prev_proc_time_;
    // 逻辑核数：用于把“相对单核”的进程 CPU% 按“占核数”口径放大（用户选择 C）
    int logical_cores = 1;
    {
        size_t len = sizeof(logical_cores);
        if (sysctlbyname("hw.logicalcpu", &logical_cores, &len, nullptr, 0) != 0) logical_cores = 1;
    }

    double a = 0, w = 0, c = 0, cf = 0, mem_used = 0, mem_total = 0;
    memory_stats(a, w, c, cf, mem_used, mem_total);

    // 把“上次 pid -> CPU 时间”整理成快速查找表
    // Build a lookup table for the previous CPU times
    out.clear();
    std::vector<ProcTick> cur;
    cur.reserve(pids.size());
    std::vector<ProcDisk> cur_disk;
    cur_disk.reserve(pids.size());
    for (pid_t pid : pids) {
        if (pid <= 0) continue;

        // 一次取回任务信息（CPU 时间、常驻内存、线程数）与 BSD 状态
        // Fetch the task info (CPU time, resident memory, threads) and BSD status in one go
        proc_taskinfo ti{};
        const bool has_ti = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) == sizeof(ti);

        ProcInfo pi;
        pi.pid = (int)pid;
        pi.name = proc_name_str(pid);
        pi.is_app = app_set.find((int)pid) != app_set.end();
        pi.frontmost = ((int)pid == front);
        pi.mem_bytes = has_ti ? (double)ti.pti_resident_size : 0;
        pi.mem_percent = mem_total > 0 ? pi.mem_bytes / mem_total * 100.0 : 0;
        pi.threads = has_ti ? (int)ti.pti_threadnum : 0;

        // 磁盘 I/O：读取累计读写字节，用两次采样差值 / dt 求速率（字节/秒）
        // Disk I/O: cumulative read/write bytes; rate = delta / dt
        uint64_t dr = 0, dw = 0;
        rusage_info_v2 ri{};
        if (proc_pid_rusage((pid_t)pid, RUSAGE_INFO_V2, (rusage_info_t*)&ri) == 0) {
            dr = ri.ri_diskio_bytesread;
            dw = ri.ri_diskio_byteswritten;
        }
        double prev_dr = 0, prev_dw = 0;
        bool d_found = false;
        for (const ProcDisk& pd : prev_disk_)
            if (pd.pid == (int)pid) { prev_dr = (double)pd.read; prev_dw = (double)pd.write; d_found = true; break; }
        pi.disk_read_bs  = (d_found && dt > 0.001) ? std::max(0.0, (double)(dr - prev_dr) / dt) : 0.0;
        pi.disk_write_bs = (d_found && dt > 0.001) ? std::max(0.0, (double)(dw - prev_dw) / dt) : 0.0;

        // 运行状态：从 BSD 进程信息映射为可读文本
        // Running status: mapped from the BSD process info
        proc_bsdinfo bi{};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, sizeof(bi)) == sizeof(bi))
            pi.status = proc_status_str(bi.pbi_status);

        // 进程 CPU% = (本次累计 CPU 时间 - 上次) / 经过时间；时间为纳秒
        // Process CPU% = (current - previous CPU time) / elapsed time; unit is nanoseconds
        const uint64_t cpu = has_ti ? (ti.pti_total_user + ti.pti_total_system) : 0;
        const double cpu_seconds = (double)cpu / 1e9;
        double prev_cpu_seconds = 0;
        bool found = false;
        for (const ProcTick& pt : prev_proc_)
            if (pt.pid == (int)pid) { prev_cpu_seconds = (double)pt.cpu / 1e9; found = true; break; }
        // 用户选择 C：把“相对单核”的 CPU% 再乘以逻辑核数，得到“占核数”口径
        pi.cpu_percent = (found && dt > 0.001) ? (cpu_seconds - prev_cpu_seconds) / dt * 100.0 * (double)logical_cores : 0.0;
        if (pi.cpu_percent < 0) pi.cpu_percent = 0;

        cur.push_back({(int)pid, cpu});
        cur_disk.push_back({(int)pid, dr, dw});
        out.push_back(std::move(pi));
    }

    prev_proc_.swap(cur);
    prev_disk_.swap(cur_disk);
    prev_proc_time_ = now;
    return true;
}

// —— 硬件信息（sysctl + IOKit）——
// Hardware info via sysctl and IOKit

namespace {

// 通过 sysctl 读取字符串类型的硬件参数
// Read a string hardware parameter via sysctl
bool sysctl_string(const char* name, std::string& out) {
    char buf[512] = {0};
    size_t len = sizeof(buf);
    if (sysctlbyname(name, buf, &len, nullptr, 0) == 0) { out = buf; return true; }
    return false;
}

// 通过 sysctl 读取 64 位整数类型的硬件参数
// Read an integer hardware parameter via sysctl
bool sysctl_i64(const char* name, int64_t& out) {
    size_t len = sizeof(out);
    return sysctlbyname(name, &out, &len, nullptr, 0) == 0;
}

// 读取 Apple 集成 GPU 的核心数：AGXAccelerator 服务暴露的 gpu-core-count 属性。
// 独立 GPU 没有此属性，返回 0 表示未知。
// Apple integrated GPU core count via the AGXAccelerator service's gpu-core-count.
int apple_gpu_core_count() {
    io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("AGXAccelerator"));
    if (!service) return 0;
    int cores = 0;
    CFTypeRef prop = IORegistryEntryCreateCFProperty(service, CFSTR("gpu-core-count"), kCFAllocatorDefault, 0);
    if (prop) {
        if (CFGetTypeID(prop) == CFNumberGetTypeID())
            CFNumberGetValue((CFNumberRef)prop, kCFNumberIntType, &cores);
        CFRelease(prop);
    }
    IOObjectRelease(service);
    return cores;
}

// 从 IORegistry 的 PCI 显示控制器（class-code 0x03）读取 GPU 名称
// Read the GPU name from the PCI display controller (class-code 0x03)
std::string pci_gpu_name() {
    CFMutableDictionaryRef matching = IOServiceMatching("IOPCIDevice");
    if (!matching) return {};
    io_iterator_t iter = 0;
    if (IOServiceGetMatchingServices(0, matching, &iter) != KERN_SUCCESS) return {};
    std::string name;
    io_registry_entry_t entry;
    while ((entry = IOIteratorNext(iter))) {
        // 读取并判断 class-code 是否为显示控制器（0x03xxxx）
        // Read the class-code and test whether it is a display controller
        uint32_t code = 0;
        CFTypeRef cc = IORegistryEntryCreateCFProperty(entry, CFSTR("class-code"), kCFAllocatorDefault, 0);
        if (cc) {
            if (CFGetTypeID(cc) == CFDataGetTypeID()) {
                const UInt8* b = CFDataGetBytePtr((CFDataRef)cc);
                if (CFDataGetLength((CFDataRef)cc) >= 3) code = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
            }
            CFRelease(cc);
        }
        if ((code >> 16) == 0x03) {
            // model 属性即 GPU 型号名（CFData 可能带尾部 NUL）
            // The model property is the GPU model name (may carry a trailing NUL)
            CFTypeRef model = IORegistryEntryCreateCFProperty(entry, CFSTR("model"), kCFAllocatorDefault, 0);
            if (model) {
                if (CFGetTypeID(model) == CFDataGetTypeID()) {
                    const UInt8* b = CFDataGetBytePtr((CFDataRef)model);
                    const size_t n = CFDataGetLength((CFDataRef)model);
                    name.assign((const char*)b, n);
                    while (!name.empty() && name.back() == '\0') name.pop_back();
                }
                CFRelease(model);
            }
            IOObjectRelease(entry);
            break;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iter);
    return name;
}

} // namespace

bool sample_cpu_info(CpuInfo& out) {
    // 芯片型号（如 "Apple M5"）
    sysctl_string("machdep.cpu.brand_string", out.model);

    int64_t phys = 0, logi = 0;
    sysctl_i64("hw.physicalcpu", phys);
    sysctl_i64("hw.logicalcpu", logi);
    out.physical = (int)phys; out.logical = (int)logi;

    // 按性能层级（hw.perflevelN.name）动态归类核心类型（Apple 官方命名）：
    //   "Super" → S 超级核心、 "Performance" → P 性能核心、 "Efficiency" → E 能效核心。
    // 遍历 hw.nperflevels 支持任意层级数（M5 两层 S+E；M6 三层 S+P+E）。
    // Classify core types by hw.perflevelN.name: Super=S, Performance=P, Efficiency=E.
    out.s_cores = out.p_cores = out.e_cores = 0;
    int64_t nlevels = 0;
    sysctl_i64("hw.nperflevels", nlevels);
    for (int i = 0; i < (int)nlevels; ++i) {
        int64_t n = 0;
        std::string name;
        sysctl_i64(("hw.perflevel" + std::to_string(i) + ".physicalcpu").c_str(), n);
        sysctl_string(("hw.perflevel" + std::to_string(i) + ".name").c_str(), name);
        if (name.find("Super") != std::string::npos)            out.s_cores += (int)n;
        else if (name.find("Performance") != std::string::npos) out.p_cores += (int)n;
        else if (name.find("Efficiency") != std::string::npos)  out.e_cores += (int)n;
        else out.p_cores += (int)n;  // 未知层级按性能核心处理
    }

    // 无性能层级（例如 Intel）时把物理核计入 P 核
    // Without perf levels (e.g. Intel) count the physical cores as P cores
    if (out.p_cores <= 0 && out.e_cores <= 0 && out.s_cores <= 0) {
        out.p_cores = out.physical; out.e_cores = 0;
    }

    // 各级缓存（字节）
    int64_t l1i = 0, l1d = 0, l2 = 0, l3 = 0;
    sysctl_i64("hw.l1icachesize", l1i);
    sysctl_i64("hw.l1dcachesize", l1d);
    sysctl_i64("hw.l2cachesize", l2);
    sysctl_i64("hw.l3cachesize", l3);
    out.cache.l1i = (double)l1i; out.cache.l1d = (double)l1d;
    out.cache.l2 = (double)l2;   out.cache.l3 = (double)l3;
    return true;
}

bool sample_gpu_info(GpuInfo& out) {
    const int agx_cores = apple_gpu_core_count();
    std::string name = pci_gpu_name();
    out.cores = agx_cores;

    if (agx_cores > 0) {
        // Apple 集成 GPU：架构固定为 Apple GPU，名字缺失时沿用芯片型号
        // Apple integrated GPU: architecture is Apple GPU; fall back to the chip model
        out.arch = sys::tr("Apple GPU（集成）", "Apple GPU (Integrated)");
        if (name.empty()) sysctl_string("machdep.cpu.brand_string", name);
    } else {
        // 独立 GPU：按名字识别厂商
        // Discrete GPU: identify the vendor from the name
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (lower.find("nvidia") != std::string::npos) out.arch = "NVIDIA";
        else if (lower.find("radeon") != std::string::npos || lower.find("amd") != std::string::npos) out.arch = "AMD";
        else if (lower.find("intel") != std::string::npos) out.arch = "Intel";
        else out.arch = name.empty() ? sys::tr("未知", "Unknown") : sys::tr("独立 GPU", "Discrete GPU");
        if (name.empty()) name = sys::tr("未知 GPU", "Unknown GPU");
    }
    out.name = name;

    // 显存 / 统一内存：Apple 芯片统一内存即系统内存上限，独立 GPU 仅近似
    // Memory: Apple chips use the system memory as unified memory; discrete GPU is approximate
    int64_t mem = 0;
    sysctl_i64("hw.memsize", mem);
    out.mem_bytes = (double)mem;
    return true;
}

// 读取 GPU 总体使用率：AGXAccelerator 的 PerformanceStatistics 里的 "Device Utilization %"
// Read the overall GPU utilization from AGXAccelerator's PerformanceStatistics
namespace {
double gpu_utilization() {
    io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("AGXAccelerator"));
    if (!service) return -1.0;
    double util = -1.0;
    CFTypeRef stats = IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
    if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
        const void* val = CFDictionaryGetValue((CFDictionaryRef)stats, CFSTR("Device Utilization %"));
        if (val && CFGetTypeID(val) == CFNumberGetTypeID()) {
            int v = 0;
            if (CFNumberGetValue((CFNumberRef)val, kCFNumberIntType, &v)) util = (double)v;
        }
    }
    if (stats) CFRelease(stats);
    IOObjectRelease(service);
    return util;
}
} // namespace

// 采样 GPU 总体使用率（%），无法获取返回 -1
// Sample the overall GPU utilization, or -1 if unavailable
double sample_gpu_utilization() {
    return gpu_utilization();
}

// 请求终止进程（kill 系统调用）。返回 0 成功；失败返回 -1 并写入 out_errno。
// Request to terminate a process via kill(). Returns 0 on success, -1 on failure.
int terminate_process(int pid, int sig, int* out_errno) {
    if (kill((pid_t)pid, sig) == 0) return 0;
    if (out_errno) *out_errno = errno;
    return -1;
}

// 探测进程是否仍存活：kill(pid, 0) 不发信号，只检查存在性与权限。
// Probe liveness without sending a signal: kill(pid, 0) only checks existence.
bool process_exists(int pid) {
    return kill((pid_t)pid, 0) == 0;
}

}
