#pragma once

// ---------------------------------------------------------------------------
// RHIGpuTimer — 异步 GPU 计时（时间戳查询）
//
// Begin/End 包住一段命令；结果在之后几帧才可用，GetMilliseconds() 返回最近一次
// 已完成的测量，从不阻塞等待 GPU。正常渲染路径里不做任何同步读回。
//   OpenGL  glQueryCounter(GL_TIMESTAMP) 成对查询，轮询 GL_QUERY_RESULT_AVAILABLE
//   Vulkan  vkCmdWriteTimestamp，帧栅栏之后读取（无 WAIT 标志）
// ---------------------------------------------------------------------------

#include "Core/Core.h"
#include "HeadLine.h"

class RHICommandBuffer;

class T_API RHIGpuTimer
{
public:
	virtual ~RHIGpuTimer() = default;

	virtual void Begin(RHICommandBuffer& cmd) = 0;
	virtual void End(RHICommandBuffer& cmd) = 0;

	// 最近一次完成的测量（毫秒）；还没有结果时返回 -1。
	virtual double GetMilliseconds() const = 0;

	// 不支持时间戳的设备返回 nullptr。
	static Ref<RHIGpuTimer> Create();
};
