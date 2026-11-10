#define GLM_ENABLE_EXPERIMENTAL
#include "VulkanExample.h"
#include <Scene/SceneRaycast.h>
#include <Scene/BVHBuilder.h>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
#ifdef RenderAPI_Vulkan
	constexpr float NearClipDepth = 0.0f;
#else
	constexpr float NearClipDepth = -1.0f;
#endif
	constexpr float HandlePixels = 72.0f;
	constexpr float HitPixels = 7.0f;
	constexpr int UniformScale = 3;
	constexpr ImU32 AxisColors[] = {
		IM_COL32(242, 83, 78, 255), IM_COL32(115, 218, 91, 255), IM_COL32(79, 155, 255, 255)
	};
	constexpr ImU32 Highlight = IM_COL32(255, 218, 92, 255);

	ImVec2 ToImVec(glm::vec2 point)
	{
		return { point.x, point.y };
	}

	struct ViewportProjection
	{
		glm::mat4 viewProjection;
		glm::vec2 origin;
		glm::vec2 size;

		bool Project(const glm::vec3& world, glm::vec2& screen) const
		{
			const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
			if (clip.w <= 0.0f || clip.z < NearClipDepth * clip.w || clip.z > clip.w)
				return false;
			// The viewport image displays its texture with V flipped.
			screen = origin + (glm::vec2(clip) / clip.w * glm::vec2(0.5f, -0.5f) + 0.5f) * size;
			return true;
		}

		glm::vec3 Unproject(glm::vec2 screen, float depth, const glm::mat4& inverse) const
		{
			const glm::vec2 ndc = ((screen - origin) / size - 0.5f) * glm::vec2(2.0f, -2.0f);
			const glm::vec4 world = inverse * glm::vec4(ndc, depth, 1.0f);
			return glm::vec3(world) / world.w;
		}
	};

	entt::entity PickMesh(const Scene& scene, const ViewportProjection& viewport, glm::vec2 mouse)
	{
		const glm::mat4 inverseViewProjection = glm::inverse(viewport.viewProjection);
		const glm::vec3 nearPoint = viewport.Unproject(mouse, NearClipDepth, inverseViewProjection);
		const glm::vec3 farPoint = viewport.Unproject(mouse, 1.0f, inverseViewProjection);
		const glm::vec3 direction = farPoint - nearPoint;
		SceneRaycast::Options options;
		options.cullBackFaces = true;
		options.renderableMeshesOnly = true;
		const auto hit = SceneRaycast::Cast(scene, nearPoint, direction, glm::length(direction), options);
		return hit ? hit->entity : entt::null;
	}

	struct HandleHit
	{
		int axis = -1;
		float distance = HitPixels;
		glm::vec2 direction{ 0.0f };
		float unitsPerPixel = 0.0f;

		void TestSegment(int candidate, glm::vec2 mouse, glm::vec2 start, glm::vec2 end,
			float units, float minLength)
		{
			const glm::vec2 segment = end - start;
			const float length = glm::length(segment);
			if (length < minLength)
				return;
			const glm::vec2 tangent = segment / length;
			const float along = std::clamp(glm::dot(mouse - start, tangent), 0.0f, length);
			const float distanceToSegment = glm::length(mouse - start - along * tangent);
			if (distanceToSegment < distance)
				*this = { candidate, distanceToSegment, tangent, units / length };
		}
	};
}

void VulkanExampleLayer::DrawViewportTools(const ImVec2& min, const ImVec2& max, bool hovered)
{
	const ImGuiIO& io = ImGui::GetIO();
	const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
	const auto& camera = m_GraphFrameData->Camera;
	const ViewportProjection viewport{
		m_TAAOutputAvailable ? camera.ViewProj : camera.JitteredViewProj,
		{ min.x, min.y }, { max.x - min.x, max.y - min.y }
	};
	auto& registry = m_Context->m_Registry;
	auto* transform = registry.valid(m_SelectedContext)
		? registry.try_get<Transform>(m_SelectedContext) : nullptr;
	auto& drag = m_TransformDrag;
	if (!transform || drag.entity != m_SelectedContext || drag.sceneRevision != m_Context->Revision ||
		!ImGui::IsWindowFocused())
		drag.entity = entt::null;

	const bool rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
	if (hovered && drag.entity == entt::null && !rightDown && !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_ViewportTool = ViewportTool::Select;
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_ViewportTool = ViewportTool::Move;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_ViewportTool = ViewportTool::Rotate;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_ViewportTool = ViewportTool::Scale;
	}

	if (drag.entity != entt::null)
	{
		const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
		const float delta = glm::dot(mouse - drag.mouseStart, drag.direction) * drag.unitsPerPixel;
		Transform edited = drag.start;
		if (!cancel && ImGui::IsMousePosValid())
		{
			switch (m_ViewportTool)
			{
			case ViewportTool::Move:
				edited.Position[drag.axis] += delta;
				break;
			case ViewportTool::Rotate:
				if (delta != 0.0f)
				{
					glm::vec3 axis(0.0f);
					axis[drag.axis] = 1.0f;
					// Transform stores Euler angles as Rx * Ry * Rz.
					const auto& r = drag.start.Rotation;
					const glm::mat4 rotation = glm::rotate(glm::eulerAngleXYZ(r.x, r.y, r.z), delta, axis);
					glm::extractEulerAngleXYZ(rotation, edited.Rotation.x, edited.Rotation.y, edited.Rotation.z);
				}
				break;
			case ViewportTool::Scale:
				for (int axis = 0; axis < 3; ++axis)
				{
					if (drag.axis == axis || drag.axis == UniformScale)
					{
						const float start = drag.start.Scale[axis];
						edited.Scale[axis] = start == 0.0f ? delta : start * std::max(0.001f, 1.0f + delta);
					}
				}
				break;
			case ViewportTool::Select:
				break;
			}
		}
		if ((cancel || ImGui::IsMousePosValid()) && (transform->Position != edited.Position ||
			transform->Rotation != edited.Rotation || transform->Scale != edited.Scale))
		{
			*transform = edited;
			BVHBuilder::MarkActiveDirty();
		}
		if (cancel || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			drag.entity = entt::null;
	}

	HandleHit hit;
	glm::vec2 center;
	if (transform && viewport.Project(transform->Position, center))
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(min, max, true);
		const float clipW = (viewport.viewProjection * glm::vec4(transform->Position, 1.0f)).w;
		const float projectionY = glm::length(glm::vec3(
			camera.ViewProj[0][1], camera.ViewProj[1][1], camera.ViewProj[2][1]));
		const float radius = HandlePixels * 2.0f * clipW / (viewport.size.y * projectionY);
		const auto& r = (drag.entity != entt::null ? drag.start : *transform).Rotation;
		const glm::mat4 axes = m_ViewportTool == ViewportTool::Move
			? glm::mat4(1.0f) : glm::eulerAngleXYZ(r.x, r.y, r.z);
		const glm::vec3 pivot = transform->Position;

		struct Segment { glm::vec2 start, end; int axis; };
		constexpr int RingSteps = 48;
		std::array<Segment, 3 * RingSteps> segments;
		size_t segmentCount = 0;
		if (m_ViewportTool == ViewportTool::Rotate)
		{
			const float step = glm::two_pi<float>() / RingSteps;
			for (int axis = 0; axis < 3; ++axis)
			{
				const glm::vec3 u(axes[(axis + 1) % 3]), v(axes[(axis + 2) % 3]);
				glm::vec2 previous;
				bool previousVisible = viewport.Project(pivot + radius * u, previous);
				for (int i = 1; i <= RingSteps; ++i)
				{
					const float angle = step * i;
					glm::vec2 point;
					const bool visible = viewport.Project(pivot + radius * (std::cos(angle) * u +
						std::sin(angle) * v), point);
					if (visible && previousVisible)
					{
						segments[segmentCount++] = { previous, point, axis };
						hit.TestSegment(axis, mouse, previous, point, step, 2.0f);
					}
					if (visible)
						previous = point;
					previousVisible = visible;
				}
			}
		}
		else if (m_ViewportTool == ViewportTool::Move || m_ViewportTool == ViewportTool::Scale)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				glm::vec2 end;
				if (viewport.Project(pivot + radius * glm::vec3(axes[axis]), end) &&
					glm::length(end - center) >= 12.0f)
				{
					segments[segmentCount++] = { center, end, axis };
					hit.TestSegment(axis, mouse, center, end,
						m_ViewportTool == ViewportTool::Move ? radius : 1.0f, 12.0f);
				}
			}
			if (m_ViewportTool == ViewportTool::Scale && glm::length(mouse - center) <= HitPixels)
				hit = { UniformScale, 0.0f, glm::normalize(glm::vec2(1.0f, -1.0f)), 1.0f / HandlePixels };
		}

		const int activeAxis = drag.entity != entt::null ? drag.axis : hovered && !rightDown ? hit.axis : -1;
		for (size_t i = 0; i < segmentCount; ++i)
		{
			const auto& [start, end, axis] = segments[i];
			const ImU32 color = activeAxis == axis ? Highlight : AxisColors[axis];
			draw->AddLine(ToImVec(start), ToImVec(end), color, 2.5f);
			if (m_ViewportTool == ViewportTool::Move)
			{
				const glm::vec2 direction = glm::normalize(end - start);
				const glm::vec2 side(-direction.y, direction.x);
				draw->AddTriangleFilled(ToImVec(end), ToImVec(end - 11.0f * direction + 5.0f * side),
					ToImVec(end - 11.0f * direction - 5.0f * side), color);
			}
			else if (m_ViewportTool == ViewportTool::Scale)
				draw->AddRectFilled(ToImVec(end - 4.0f), ToImVec(end + 4.0f), color);
		}
		if (m_ViewportTool == ViewportTool::Scale)
			draw->AddRectFilled(ToImVec(center - 5.0f), ToImVec(center + 5.0f),
				activeAxis == UniformScale ? Highlight : IM_COL32_WHITE);
		else
			draw->AddCircleFilled(ToImVec(center), 3.0f, Highlight);
		draw->PopClipRect();
	}

	if (hovered && !rightDown && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hit.axis >= 0)
			drag = { m_SelectedContext, m_Context->Revision, hit.axis, *transform, mouse,
				hit.direction, hit.unitsPerPixel };
		else
			m_SelectedContext = PickMesh(*m_Context, viewport, mouse);
	}
}
