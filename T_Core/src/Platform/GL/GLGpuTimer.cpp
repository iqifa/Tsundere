#include "Platform/RHI/RHIGpuTimer.h"
#include "Platform/RenderAPIConfig.h"
#include "GLDebug.h"
#include <GL/glew.h>

#include <array>

#ifdef RenderAPI_OpenGL
namespace
{
	// 成对的 GL_TIMESTAMP 查询（可嵌套，不像 GL_TIME_ELAPSED 那样互斥）。
	// 环形 4 槽：结果通常晚 1~2 帧可用，只轮询 AVAILABLE，从不阻塞。
	class GLGpuTimer final : public RHIGpuTimer
	{
	public:
		GLGpuTimer()
		{
			GLCall(glGenQueries(static_cast<GLsizei>(m_Queries.size()), m_Queries.data()));
		}

		~GLGpuTimer() override
		{
			GLCall(glDeleteQueries(static_cast<GLsizei>(m_Queries.size()), m_Queries.data()));
		}

		void Begin(RHICommandBuffer&) override
		{
			Harvest();
			Slot& slot = m_Slots[m_Next];
			m_Active = !slot.pending;   // 该槽结果还没回来：本帧跳过测量
			if (!m_Active)
				return;
			GLCall(glQueryCounter(m_Queries[m_Next * 2], GL_TIMESTAMP));
		}

		void End(RHICommandBuffer&) override
		{
			if (!m_Active)
				return;
			GLCall(glQueryCounter(m_Queries[m_Next * 2 + 1], GL_TIMESTAMP));
			m_Slots[m_Next].pending = true;
			m_Next = (m_Next + 1) % kSlots;
			m_Active = false;
		}

		double GetMilliseconds() const override { return m_Last; }

	private:
		static constexpr size_t kSlots = 4;
		struct Slot { bool pending = false; };

		void Harvest()
		{
			for (size_t i = 0; i < kSlots; ++i)
			{
				if (!m_Slots[i].pending)
					continue;
				GLint available = 0;
				glGetQueryObjectiv(m_Queries[i * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &available);
				if (!available)
					continue;
				GLuint64 begin = 0, end = 0;
				glGetQueryObjectui64v(m_Queries[i * 2], GL_QUERY_RESULT, &begin);
				glGetQueryObjectui64v(m_Queries[i * 2 + 1], GL_QUERY_RESULT, &end);
				if (end >= begin)
					m_Last = static_cast<double>(end - begin) * 1e-6;
				m_Slots[i].pending = false;
			}
		}

		std::array<GLuint, kSlots * 2> m_Queries{};
		std::array<Slot, kSlots> m_Slots{};
		size_t m_Next = 0;
		bool m_Active = false;
		double m_Last = -1.0;
	};
}

Ref<RHIGpuTimer> RHIGpuTimer::Create()
{
	return CreateRef<GLGpuTimer>();
}
#endif
