#pragma once

// ---------------------------------------------------------------------------
// AssetState — 资产加载生命周期的完整状态机
//
// 状态转换路径：
//
//   Unloaded
//     ↓  Request()
//   Queued
//     ↓  worker 线程拾取
//   LoadingCPU
//     ↓  CPU 加载完成（解码/解析）
//   CPUReady
//     ↓  主线程 GPU 上传
//   GPUReady          ←── 渲染可用
//     ↓  LRU 超时（仅驱逐 GPU）
//   GPUEvicted        ←── CPU 数据保留，重传无需读盘
//     ↓  LRU 超时（或 CPU 内存超预算）
//   Unloaded
//
//   任何阶段失败 → Failed
// ---------------------------------------------------------------------------

enum class AssetState : uint8_t {
    Unloaded,     // 无任何数据
    Queued,       // 已提交请求，等待 worker 拾取
    LoadingCPU,   // worker 线程正在读磁盘/解码/解析
    CPUReady,     // CPU 数据就绪，等待主线程 GPU 上传
    GPUReady,     // GPU 资源就绪，可以渲染
    GPUEvicted,   // GPU 资源已回收，CPU 数据还在（快速重传）
    Failed,       // 加载失败（文件不存在、格式错误等）
};
