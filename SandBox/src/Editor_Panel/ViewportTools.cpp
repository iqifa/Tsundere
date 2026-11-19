#define GLM_ENABLE_EXPERIMENTAL
#include "VulkanExample.h"
#include <Scene/SceneRaycast.h>
#include <Scene/SceneChanges.h>
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

	// World units covered by one screen pixel at `world` (for screen-constant overlay sizes).
	float WorldPerPixel(const ViewportProjection& viewport, const glm::vec3& world)
	{
		const glm::mat4& m = viewport.viewProjection;
		const float clipW = (m * glm::vec4(world, 1.0f)).w;
		const float projectionY = glm::length(glm::vec3(m[0][1], m[1][1], m[2][1]));
		return 2.0f * clipW / (viewport.size.y * projectionY);
	}

	// ---- Light overlay ------------------------------------------------------

	constexpr float LightPickPixels = 12.0f;
	constexpr int CircleSteps = 64;

	// Light color normalised to its brightest channel so dim or HDR colors stay readable.
	ImU32 LightTint(const Component::Light& light, int alpha)
	{
		glm::vec3 c = glm::max(light.Color, glm::vec3(0.0f));
		const float peak = std::max({ c.x, c.y, c.z });
		c = peak > 1e-4f ? c / peak : glm::vec3(1.0f);
		c = glm::mix(glm::vec3(1.0f), c, 0.75f);   // keep near-black / saturated colors visible
		return IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), alpha);
	}

	glm::vec3 SafeDirection(const glm::vec3& d)
	{
		const float length = glm::length(d);
		return length > 1e-6f ? d / length : glm::vec3(0.0f, -1.0f, 0.0f);
	}

	// Same basis as BuildLightBasis in res/shaders/include/Lighting.glsl.
	void LightBasis(const glm::vec3& n, glm::vec3& right, glm::vec3& up)
	{
		const glm::vec3 ref = std::abs(n.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		right = glm::normalize(glm::cross(ref, n));
		up = glm::cross(n, right);
	}

	// Draws only segments whose both ends are in front of the camera.
	void DrawWorldLine(ImDrawList* draw, const ViewportProjection& viewport,
		const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness)
	{
		glm::vec2 sa, sb;
		if (viewport.Project(a, sa) && viewport.Project(b, sb))
			draw->AddLine(ToImVec(sa), ToImVec(sb), color, thickness);
	}

	void DrawWorldCircle(ImDrawList* draw, const ViewportProjection& viewport, const glm::vec3& center,
		const glm::vec3& u, const glm::vec3& v, float radius, ImU32 color, float thickness)
	{
		const float step = glm::two_pi<float>() / CircleSteps;
		glm::vec2 previous;
		bool previousVisible = viewport.Project(center + radius * u, previous);
		for (int i = 1; i <= CircleSteps; ++i)
		{
			glm::vec2 point;
			const float angle = step * i;
			const bool visible = viewport.Project(center + radius * (std::cos(angle) * u + std::sin(angle) * v), point);
			if (visible && previousVisible)
				draw->AddLine(ToImVec(previous), ToImVec(point), color, thickness);
			previous = point;
			previousVisible = visible;
		}
	}

	void DrawWorldArrow(ImDrawList* draw, const ViewportProjection& viewport,
		const glm::vec3& from, const glm::vec3& to, ImU32 color, float thickness)
	{
		glm::vec2 a, b;
		if (!viewport.Project(from, a) || !viewport.Project(to, b))
			return;
		draw->AddLine(ToImVec(a), ToImVec(b), color, thickness);
		const glm::vec2 d = b - a;
		if (glm::length(d) < 8.0f)
			return;
		const glm::vec2 dir = glm::normalize(d), side(-dir.y, dir.x);
		draw->AddTriangleFilled(ToImVec(b), ToImVec(b - 10.0f * dir + 4.5f * side),
			ToImVec(b - 10.0f * dir - 4.5f * side), color);
	}

	// Range / shape wireframe in world space.
	void DrawLightExtent(ImDrawList* draw, const ViewportProjection& viewport, const glm::vec3& position,
		const Component::Light& light, ImU32 color, ImU32 faint)
	{
		using Type = Component::Light::LightType;
		const glm::vec3 dir = SafeDirection(light.Direction);
		glm::vec3 right, up;
		LightBasis(dir, right, up);
		const float wpp = WorldPerPixel(viewport, position);

		switch (light.type)
		{
		case Type::Directional:
			// Parallel rays: a short bundle of arrows in the propagation direction.
			for (int i = 0; i < 5; ++i)
			{
				const glm::vec3 offset = i == 0 ? glm::vec3(0.0f)
					: 9.0f * wpp * (i == 1 ? right : i == 2 ? -right : i == 3 ? up : -up);
				DrawWorldArrow(draw, viewport, position + offset, position + offset + dir * (70.0f * wpp),
					i == 0 ? color : faint, i == 0 ? 2.0f : 1.5f);
			}
			break;

		case Type::Point:
		{
			const float radius = light.Params.Attenuation.Radius;
			if (radius <= 0.0f)
				break;
			const glm::vec3 x(1, 0, 0), y(0, 1, 0), z(0, 0, 1);
			DrawWorldCircle(draw, viewport, position, x, y, radius, color, 1.5f);
			DrawWorldCircle(draw, viewport, position, y, z, radius, color, 1.5f);
			DrawWorldCircle(draw, viewport, position, z, x, radius, color, 1.5f);
			break;
		}

		case Type::Spot:
		{
			const float range = light.Params.Spot.Radius;
			if (range <= 0.0f)
				break;
			// Cone of slant length = range; apex at the light. Outer angle solid, inner faint.
			const auto cone = [&](float degrees, ImU32 c, float thickness, bool edges)
			{
				const float angle = glm::radians(glm::clamp(degrees, 0.0f, 89.0f));
				const glm::vec3 baseCenter = position + dir * (range * std::cos(angle));
				const float baseRadius = range * std::sin(angle);
				DrawWorldCircle(draw, viewport, baseCenter, right, up, baseRadius, c, thickness);
				if (edges)
					for (const glm::vec3& side : { right, -right, up, -up })
						DrawWorldLine(draw, viewport, position, baseCenter + side * baseRadius, c, thickness);
			};
			cone(light.Params.Spot.OuterAngle, color, 1.5f, true);
			if (light.Params.Spot.InnerAngle > 0.0f && light.Params.Spot.InnerAngle < light.Params.Spot.OuterAngle)
				cone(light.Params.Spot.InnerAngle, faint, 1.0f, false);
			DrawWorldLine(draw, viewport, position, position + dir * range, faint, 1.0f);
			break;
		}

		case Type::Area:
		{
			const auto& area = light.Params.Area;
			if (area.Shape == 1)
				DrawWorldCircle(draw, viewport, position, right, up, 0.5f * area.Width, color, 1.5f);
			else
			{
				const glm::vec3 r = right * (0.5f * area.Width), u = up * (0.5f * area.Height);
				const glm::vec3 corners[4] = { position - r - u, position + r - u, position + r + u, position - r + u };
				for (int i = 0; i < 4; ++i)
					DrawWorldLine(draw, viewport, corners[i], corners[(i + 1) % 4], color, 1.5f);
			}
			// One-sided emitter: show which way it faces.
			DrawWorldArrow(draw, viewport, position, position + dir * (40.0f * wpp), faint, 1.5f);
			break;
		}
		}
	}

	// Screen-space icon, one glyph per light type.
	void DrawLightIcon(ImDrawList* draw, const ViewportProjection& viewport, const glm::vec3& position,
		const Component::Light& light, glm::vec2 c, ImU32 color, bool selected)
	{
		using Type = Component::Light::LightType;
		const ImVec2 p = ToImVec(c);
		draw->AddCircleFilled(p, 11.0f, IM_COL32(20, 22, 26, 170));
		draw->AddCircle(p, 11.0f, selected ? Highlight : IM_COL32(255, 255, 255, 60), 0, selected ? 2.0f : 1.0f);

		switch (light.type)
		{
		case Type::Directional:   // sun
			draw->AddCircleFilled(p, 3.5f, color);
			for (int i = 0; i < 8; ++i)
			{
				const float a = glm::two_pi<float>() * i / 8.0f;
				const glm::vec2 d(std::cos(a), std::sin(a));
				draw->AddLine(ToImVec(c + 5.5f * d), ToImVec(c + 8.5f * d), color, 1.5f);
			}
			break;

		case Type::Point:         // bulb: ring + core
			draw->AddCircle(p, 6.5f, color, 0, 1.8f);
			draw->AddCircleFilled(p, 3.0f, color);
			break;

		case Type::Spot:          // cone pointing along the projected light direction
		{
			glm::vec2 tip;
			glm::vec2 d(0.0f, 1.0f);
			const float wpp = WorldPerPixel(viewport, position);
			if (viewport.Project(position + SafeDirection(light.Direction) * (20.0f * wpp), tip) &&
				glm::length(tip - c) > 1.0f)
				d = glm::normalize(tip - c);
			const glm::vec2 side(-d.y, d.x);
			const glm::vec2 apex = c - 6.0f * d;
			draw->AddTriangleFilled(ToImVec(apex), ToImVec(c + 5.0f * d + 6.0f * side),
				ToImVec(c + 5.0f * d - 6.0f * side), color);
			break;
		}

		case Type::Area:          // panel
			if (light.Params.Area.Shape == 1)
			{
				draw->AddCircleFilled(p, 6.0f, (color & 0x00FFFFFF) | 0x60000000);
				draw->AddCircle(p, 6.0f, color, 0, 1.5f);
			}
			else
			{
				draw->AddRectFilled(ToImVec(c - glm::vec2(7.0f, 5.0f)), ToImVec(c + glm::vec2(7.0f, 5.0f)),
					(color & 0x00FFFFFF) | 0x60000000);
				draw->AddRect(ToImVec(c - glm::vec2(7.0f, 5.0f)), ToImVec(c + glm::vec2(7.0f, 5.0f)), color, 0.0f, 0, 1.5f);
			}
			break;
		}
	}

	entt::entity DrawLightOverlay(Scene& scene, entt::entity selectedEntity, bool showIcons, bool showAllRanges,
		const ViewportProjection& viewport, const ImVec2& min, const ImVec2& max, glm::vec2 mouse);

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
			SceneChanges::NotifyTransform(m_Context->m_Registry, drag.entity);
		}
		if (cancel || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
			drag.entity = entt::null;
	}

	const entt::entity hoveredLight = DrawLightOverlay(*m_Context, m_SelectedContext,
		m_ShowLightIcons, m_ShowAllLightRanges, viewport, min, max, mouse);

	HandleHit hit;
	glm::vec2 center;
	if (transform && viewport.Project(transform->Position, center))
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(min, max, true);
		const float radius = HandlePixels * WorldPerPixel(viewport, transform->Position);
		// Follow the edited orientation continuously, including the release frame.
		// The drag calculation above still uses its fixed starting transform.
		const auto& r = transform->Rotation;
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
		else if (hoveredLight != entt::null)
			m_SelectedContext = hoveredLight;   // lights have no mesh: the icon is their pick target
		else
			m_SelectedContext = PickMesh(*m_Context, viewport, mouse);
	}
}

namespace
{
entt::entity DrawLightOverlay(Scene& scene, entt::entity selectedEntity, bool showIcons, bool showAllRanges,
	const ViewportProjection& viewport,
	const ImVec2& min, const ImVec2& max, glm::vec2 mouse)
{
	auto& registry = scene.m_Registry;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(min, max, true);

	entt::entity hovered = entt::null;
	float hoveredDistance = LightPickPixels;
	for (auto [entity, transform, light] : registry.view<Transform, Light>().each())
	{
		const bool selected = entity == selectedEntity;
		const bool active = scene.IsActive(entity);
		const int alpha = active ? 255 : 110;

		if (selected || showAllRanges)
			DrawLightExtent(draw, viewport, transform.Position, light,
				LightTint(light, selected ? alpha * 9 / 10 : alpha / 2), LightTint(light, alpha * 2 / 5));

		glm::vec2 center;
		if (!showIcons || !viewport.Project(transform.Position, center))
			continue;
		DrawLightIcon(draw, viewport, transform.Position, light, center, LightTint(light, alpha), selected);
		const float distance = glm::length(mouse - center);
		if (distance < hoveredDistance)
		{
			hoveredDistance = distance;
			hovered = entity;
		}
	}
	draw->PopClipRect();
	return hovered;
}
}
