#pragma once

#include <functional>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

namespace Engine {

// Minimal FIFO job queue for a single worker thread.
// Main thread Submit()s jobs; worker thread WaitForJob()/Execute()s them.
// No thread pool — one persistent worker is enough for asset loading.
class JobSystem
{
public:
	using JobCallback = std::function<void()>;

	static void Init();
	static void Shutdown();

	// Submit a job. Returns immediately. Job executes on the worker thread.
	static void Submit(JobCallback job);

	// Internal: wait for next job (blocking, called by worker thread)
	static bool WaitForJob(JobCallback& outJob);

private:
	static void WorkerLoop();

	static std::deque<JobCallback> s_Queue;
	static std::mutex s_Mutex;
	static std::condition_variable s_Condition;
	static std::atomic<bool> s_Running;
	static std::thread s_Worker;
};

} // namespace Engine
