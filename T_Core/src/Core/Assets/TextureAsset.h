#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// TextureAsset — 纯 CPU 侧贴图数据
//
// 从磁盘读取、解码后的像素数据。没有任何 OpenGL 状态，
// 可以在任意线程安全创建、移动、销毁。
//
// 来源：ResourceLoader::ExecuteTextureLoad（worker 线程）
// 消费：GPUTexture::CreateFromAsset（主线程 GL 上传后释放 pixels）
// ---------------------------------------------------------------------------
struct TextureAsset {
    std::string           path;
    std::vector<uint8_t>  pixels;   // 解码后的像素数据（RGBA，强制4通道）
    int                   width    = 0;
    int                   height   = 0;
    int                   channels = 4;  // 强制输出4通道，与 pixels 实际布局一致

    // 上传 GPU 后是否保留 CPU 副本（用于 BVH、物理碰撞等需要 CPU 数据的场景）
    bool keepCPUCopy = false;

    bool IsValid() const { return !pixels.empty(); }

    // 预估 CPU 内存占用
    size_t CPUSize() const { return pixels.size(); }
};
