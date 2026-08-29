#pragma once

#include <vector>

// 历史曲线数据层：记录 CPU 各核 / CPU 总体 / 内存各分类 / GPU 的使用率采样，
// 以及累计采样次数。纯数据结构 + 通用追加/平滑工具，便于单独测试与后续拆分。
// Usage-history data layer: keeps per-core / overall CPU, memory categories and GPU
// usage samples plus the total sample counter. Pure data + generic append helpers.
struct History {
    // 历史曲线保留的采样点数量
    // Number of samples kept in each history buffer
    static constexpr int kHist = 60;

    std::vector<std::vector<float>> core_hist;      // [逻辑核索引][历史采样] per-core [index][sample]
    std::vector<float> cpu_hist;                    // CPU 总体使用率历史（平均所有核）
    std::vector<float> mem_hist;                    // 内存占用率历史（用于压力图）
    std::vector<float> mem_app_hist;                // App 内存占比历史
    std::vector<float> mem_wired_hist;              // 联动内存占比历史
    std::vector<float> mem_compressed_hist;         // 被压缩占比历史
    std::vector<float> mem_cached_hist;             // 已缓存文件占比历史
    std::vector<float> gpu_hist;                    // GPU 总体使用率历史
    std::vector<float> disk_read_hist;              // 磁盘读速率历史（字节/秒）
    std::vector<float> disk_write_hist;             // 磁盘写速率历史（字节/秒）
    float gpu_util = 0;                             // 当前 GPU 使用率（%）
    int sample_total = 0;                           // 累计采样次数（驱动竖线网格随采样滚动）

    // 向历史缓冲追加采样点，超过上限时丢弃最旧的一个
    // Append a sample to a history buffer, dropping the oldest when full
    static void push(std::vector<float>& h, float v) {
        if (h.size() >= (size_t)kHist) h.erase(h.begin());
        h.push_back(v);
    }

    // 追加采样点并做指数平滑（EMA）：新值 = α×新 + (1-α)×上次显示值。
    // 削弱相邻采样的剧烈跳变，避免折线过于离散/锯齿，让曲线更平滑可读。
    // Append a sample with exponential smoothing (EMA) to reduce jumps between samples.
    static void push_smooth(std::vector<float>& h, float v) {
        constexpr float kAlpha = 0.5f;
        if (!h.empty()) v = kAlpha * v + (1.0f - kAlpha) * h.back();
        push(h, v);
    }
};
