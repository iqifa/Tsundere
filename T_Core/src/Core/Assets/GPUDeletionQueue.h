#pragma once
#include <mutex>
#include <vector>
#include <functional>

// ---------------------------------------------------------------------------
// GPUDeletionQueue — 跨线程安全的 GL 资源延迟删除队列
//
// 问题背景：
//   glDeleteTextures / glDeleteBuffers 等必须在 GL 线程（主线程）调用。
//   但 Ref<T>（shared_ptr）最后一个引用可能在任意线程析构，
//   导致 GL 调用从非 GL 线程发出 → 未定义行为 / driver crash。
//
// 解决方案：
//   析构时不直接调用 glDelete*，而是把 lambda 放入本队列。
//   主线程在每帧开头调用 Flush()，统一执行所有挂起的 GL 删除。
//
// 用法（在 GL 资源析构函数里）：
//   ~GPUTexture() {
//       unsigned id = m_ID;
//       GPUDeletionQueue::Enqueue([id]{ glDeleteTextures(1, &id); });
//   }
//
//   // Application::Run() 每帧开头
//   GPUDeletionQueue::Flush();
// ---------------------------------------------------------------------------
class GPUDeletionQueue {
public:
    using DeleteFn = std::function<void()>;

    // 线程安全：可从任意线程调用
    static void Enqueue(DeleteFn fn) {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Queue.push_back(std::move(fn));
    }

    // 只能在主 GL 线程调用（每帧一次）
    static void Flush() {
        std::vector<DeleteFn> pending;
        {
            std::lock_guard<std::mutex> lock(s_Mutex);
            pending.swap(s_Queue);
        }
        // 在锁外执行，避免 glDelete 里发生二次 Enqueue 时死锁
        for (auto& fn : pending)
            fn();
    }

    static bool IsEmpty() {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Queue.empty();
    }

private:
    static inline std::mutex          s_Mutex;
    static inline std::vector<DeleteFn> s_Queue;
};
