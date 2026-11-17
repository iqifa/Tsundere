#pragma once

// ---------------------------------------------------------------------------
// SceneChanges — 分类修改通知（版本号），驱动 Path Tracing 等派生数据的增量更新
//
// 修改方在改完数据之后调用 Notify*，对应类别的全局版本号 +1。版本号只增不减、
// 没有“清除”操作：每个消费方（PathTraceScene 等）各自记录上次处理到的版本，
// 互不干扰，也不会因为别人先消费而丢更新。消费方在处理前先取快照，处理期间
// 再发生的修改会让版本继续前进，下一帧照常被看到。
//
//   类别                 典型来源                               消费范围（PT）
//   MeshGeometry         Mesh 顶点 / 索引就地修改               该 Mesh 的 BLAS + TLAS
//   InstanceTransform    Transform 修改                         实例数据 + TLAS（复用 BLAS）
//   InstanceSet          实体 / MeshRender 增删、模型加载、     实例列表 + TLAS，按需建/删 BLAS
//                        子网格启用、实体启用 / 层级变化
//   MaterialAssignment   MeshRender::materials 槽位变化          材质映射 / 实例 materialIndex
//   MaterialParameters   Material 参数（Material 的 setter 自动） 材质 buffer
//   Lights               Light 组件、灯光实体的 Transform / 启用  灯光 buffer
//
// 自动覆盖（无需手动通知）：
//   * MeshRender / Light 组件的 emplace / remove / patch、实体销毁（EnTT 信号，Scene 构造时挂接）
//   * Material 的所有 setter / SetShader / Deserialize / 赋值
//   * Entity 的层级修改函数（addchildwithchangeparent / setparenwithdelself）
// 其余“拿引用直接改字段”的地方（Inspector、Gizmo、加载器……）必须手动调用。
//
// 开发期漏标校验：ENABLE_PT_DIRTY_VALIDATION（Debug 配置由 premake 定义为 1），
// 见 Pipeline/PathTracing/PTDirtyValidation.h。
// ---------------------------------------------------------------------------

#include "Core/Core.h"
#include <entt/entt.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#ifndef ENABLE_PT_DIRTY_VALIDATION
#define ENABLE_PT_DIRTY_VALIDATION 0
#endif

class Mesh;

namespace SceneChanges
{
	enum class Category : uint32_t
	{
		MeshGeometry = 0,
		InstanceTransform,
		InstanceSet,
		MaterialAssignment,
		MaterialParameters,
		Lights,
		Count
	};
	constexpr size_t CategoryCount = static_cast<size_t>(Category::Count);

	constexpr uint32_t Bit(Category c) { return 1u << static_cast<uint32_t>(c); }
	constexpr uint32_t AllCategories = (1u << CategoryCount) - 1u;
	// 影响实例 / 加速结构的类别
	constexpr uint32_t GeometryCategories = Bit(Category::MeshGeometry) | Bit(Category::InstanceTransform) |
		Bit(Category::InstanceSet) | Bit(Category::MaterialAssignment);

	T_API const char* CategoryName(Category c);
	// "InstanceSet|Lights" 形式，日志 / UI 用
	T_API std::string DescribeMask(uint32_t mask);

	struct Versions
	{
		std::array<uint64_t, CategoryCount> value{};

		uint64_t operator[](Category c) const { return value[static_cast<size_t>(c)]; }
		// 相对 since 有变化的类别掩码
		uint32_t ChangedSince(const Versions& since) const
		{
			uint32_t mask = 0;
			for (size_t i = 0; i < CategoryCount; ++i)
				if (value[i] != since.value[i])
					mask |= 1u << i;
			return mask;
		}
	};

	// ---- 通用 ----
	T_API void Notify(Category c);
	T_API void NotifyMask(uint32_t mask);
	T_API uint64_t Version(Category c);
	T_API Versions Snapshot();

	// ---- 便捷入口 ----
	// 实体 Transform 改了：带 MeshRender → InstanceTransform；带 Light → Lights。
	T_API void NotifyTransform(const entt::registry& registry, entt::entity entity);
	// 实体启用状态 / 父子层级改了（影响整棵子树的可见性）：InstanceSet + Lights。
	T_API void NotifyActivation();
	// 模型内容换了（加载完成、子网格启用 / 删除）：InstanceSet + MaterialAssignment。
	T_API void NotifyModelChanged();
	// MeshRender 的 ModelPath 换了：InstanceSet + MaterialAssignment。
	T_API void NotifyModelBinding();
	// MeshRender::materials 槽位改了（增删 / 替换 / resize）。
	T_API void NotifyMaterialAssignment();
	// Mesh 的顶点 / 索引就地改了：该 Mesh 的几何版本 +1（BLAS 重建）+ MeshGeometry。
	T_API void NotifyMeshGeometry(Mesh& mesh);

	// 给 registry 挂 EnTT 信号（MeshRender / Light 的构造、销毁、patch）。Scene 构造时调用。
	T_API void Attach(entt::registry& registry);

	// 全进程唯一的几何 ID（Mesh 用；拷贝得到新 ID，移动沿用 ID）。
	T_API uint64_t NewGeometryId();
}

// 嵌在 Mesh 里的几何身份：BLAS 缓存以 (Id, Mesh::GeometryRevision) 为键。
// 拷贝 = 新的几何（新 ID，之后各改各的）；移动 = 同一份几何换了位置（vector 擦除 /
// 扩容时 ID 跟着数据走，BLAS 缓存因此不失效）。
class MeshGeometryId
{
public:
	MeshGeometryId() : m_Id(SceneChanges::NewGeometryId()) {}
	MeshGeometryId(const MeshGeometryId&) : m_Id(SceneChanges::NewGeometryId()) {}
	MeshGeometryId(MeshGeometryId&& other) noexcept : m_Id(other.m_Id) { other.m_Id = SceneChanges::NewGeometryId(); }
	MeshGeometryId& operator=(const MeshGeometryId& other)
	{
		if (this != &other)
			m_Id = SceneChanges::NewGeometryId();
		return *this;
	}
	MeshGeometryId& operator=(MeshGeometryId&& other) noexcept
	{
		if (this != &other)
		{
			m_Id = other.m_Id;
			other.m_Id = SceneChanges::NewGeometryId();
		}
		return *this;
	}
	uint64_t Get() const { return m_Id; }

private:
	uint64_t m_Id;
};
