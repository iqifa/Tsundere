// PT 分类脏标记 / 增量几何 / 漏标校验（CPU 部分，不需要 GPU 上下文）。
// 运行：tests\RunPTDirtyTests.cmd（先构建 T_Core）。
#include "Scene/BVHBuilder.h"
#include "Scene/SceneChanges.h"
#include "Pipeline/PathTracing/PTDirtyValidation.h"
#include <cassert>
#include <iostream>

using SceneChanges::Bit;
using SceneChanges::Category;

namespace
{
	Mesh Triangle(float z)
	{
		Mesh mesh;
		mesh.vertices.resize(3);
		mesh.vertices[0].Position = { -1, -1, z };
		mesh.vertices[1].Position = { 1, -1, z };
		mesh.vertices[2].Position = { 0, 1, z };
		mesh.indices = { 0, 1, 2 };
		return mesh;
	}

	uint32_t ChangedSince(const SceneChanges::Versions& before)
	{
		return SceneChanges::Snapshot().ChangedSince(before);
	}
}

int main()
{
	Engine::Log::Init();
	Scene scene;

	// ---- 模型：A 有两个子网格（两个实体共用），B 一个子网格 ----
	auto modelA = CreateRef<Model>();
	modelA->meshes.push_back(Triangle(0));
	modelA->meshes.push_back(Triangle(-1));
	auto modelB = CreateRef<Model>();
	modelB->meshes.push_back(Triangle(-2));
	My_map::m_ModleMap["pt-test-a"] = modelA;
	My_map::m_ModleMap["pt-test-b"] = modelB;

	// Mesh 几何身份：移动沿用 ID，拷贝换新 ID
	{
		Mesh a = Triangle(0);
		const uint64_t id = a.GeometryId.Get();
		Mesh moved = std::move(a);
		assert(moved.GeometryId.Get() == id);
		Mesh copied = moved;
		assert(copied.GeometryId.Get() != id);
	}

	// ---- 通知：EnTT 信号 ----
	auto v = SceneChanges::Snapshot();
	auto e1 = scene.CreateEntity("A1");
	auto e2 = scene.CreateEntity("A2");
	auto e3 = scene.CreateEntity("B");
	assert(ChangedSince(v) == 0);   // 空实体不影响 PT
	e1.AddComponent<Component::MeshRender>().ModelPath = "pt-test-a";
	e2.AddComponent<Component::MeshRender>().ModelPath = "pt-test-a";
	e3.AddComponent<Component::MeshRender>().ModelPath = "pt-test-b";
	assert(ChangedSince(v) == (Bit(Category::InstanceSet) | Bit(Category::MaterialAssignment)));

	v = SceneChanges::Snapshot();
	auto lamp = scene.CreateEntity("Lamp");
	lamp.AddComponent<Component::Light>();
	assert(ChangedSince(v) == Bit(Category::Lights));
	v = SceneChanges::Snapshot();
	SceneChanges::NotifyTransform(scene.m_Registry, lamp);
	assert(ChangedSince(v) == Bit(Category::Lights));
	v = SceneChanges::Snapshot();
	SceneChanges::NotifyTransform(scene.m_Registry, e1);
	assert(ChangedSince(v) == Bit(Category::InstanceTransform));

	// 材质 setter 自动通知并递增材质版本
	auto red = CreateRef<Engine::Material>();
	auto blue = CreateRef<Engine::Material>();
	v = SceneChanges::Snapshot();
	const uint64_t revision = red->GetRevision();
	red->SetVec4("u_BaseColor", glm::vec4(1, 0, 0, 1));
	assert(red->GetRevision() == revision + 1 && ChangedSince(v) == Bit(Category::MaterialParameters));

	// 层级修改通知启用状态
	v = SceneChanges::Snapshot();
	e1.addchildwithchangeparent(lamp);
	assert(ChangedSince(v) == (Bit(Category::InstanceSet) | Bit(Category::Lights)));

	// ---- 增量几何 ----
	std::unordered_map<Engine::Material*, unsigned int> materialMap{ { red.get(), 1 }, { blue.get(), 2 } };
	e1.GetComponent<Component::MeshRender>().materials = { red, red };
	BVHBuilder builder;
	builder.SetMaterialMap(&materialMap);

	auto r = builder.UpdateIncremental(scene, false);
	assert(r.geometryChanged && r.instancesChanged && r.blasBuilt == 3);
	assert(builder.GetInstanceCount() == 5 && builder.GetStats().meshCount == 3);
	const std::vector<uint64_t> keys = builder.GetBLASKeys();

	// 什么都没变：无任何工作
	r = builder.UpdateIncremental(scene, false);
	assert(!r.Changed() && r.blasBuilt == 0 && r.blasReleased == 0);

	// 移动物体：只重建 TLAS，BLAS 全部复用
	e2.GetComponent<Component::Transform>().Position = { 5, 0, 0 };
	r = builder.UpdateIncremental(scene, false);
	assert(!r.geometryChanged && r.instancesChanged && r.blasBuilt == 0 && r.blasReused == 3);
	assert(builder.GetBLASKeys() == keys);

	// 材质分配：只修补 materialIndex，不重建任何树
	const auto tlasBefore = builder.GetData().tlasNodes.size();
	e1.GetComponent<Component::MeshRender>().materials = { blue, red };
	r = builder.UpdateIncremental(scene, false);
	assert(r.materialsPatched && !r.instancesChanged && !r.geometryChanged && r.blasBuilt == 0);
	assert(builder.GetData().tlasNodes.size() == tlasBefore);
	{
		bool sawBlue = false;
		for (const auto& instance : builder.GetData().instances)
			sawBlue = sawBlue || instance.materialIndex == 2;
		assert(sawBlue);
	}

	// Mesh 顶点就地修改 + 通知：拓扑没变，只 refit 这一个 BLAS
	modelA->meshes[1].vertices[2].Position.y = 2;
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.geometryChanged && r.blasRefitted == 1 && r.blasBuilt == 0 && r.blasReused == 2 && r.blasReleased == 0);
	{
		// 硬件后端提示：被 refit 的槽位指向旧键、标为动态；其余槽位无提示
		const auto& hints = builder.GetBLASHints();
		const auto& now = builder.GetBLASKeys();
		assert(hints.size() == now.size());
		int refitSlots = 0;
		for (size_t i = 0; i < hints.size(); ++i)
		{
			if (hints[i].refitFrom == 0)
			{
				assert(!hints[i].dynamic && now[i] == keys[i]);
				continue;
			}
			++refitSlots;
			assert(hints[i].dynamic && hints[i].refitFrom == keys[i] && now[i] != keys[i]);
		}
		assert(refitSlots == 1);
		// TLAS 也跟着重建（实例包围盒变了），树内容与新顶点一致
		const auto& data = builder.GetData();
		bool sawMovedVertex = false;
		for (const auto& tri : data.triangles)
			sawMovedVertex = sawMovedVertex || tri.v2.y == 2.0f;
		assert(sawMovedVertex && r.instancesChanged);
	}
	{
		// 拼接顺序保持不变，只有被改的那个网格换了键
		const auto& now = builder.GetBLASKeys();
		assert(now.size() == keys.size());
		int differing = 0;
		for (size_t i = 0; i < keys.size(); ++i)
			differing += now[i] != keys[i];
		assert(differing == 1);
	}

	// 下一次无关更新：提示清空（refitFrom 只描述最近一次更新），动态标记保留
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	modelA->meshes[1].vertices[2].Position.y = 2.5f;
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 1);
	e2.GetComponent<Component::Transform>().Position = { 6, 0, 0 };
	r = builder.UpdateIncremental(scene, false);
	assert(r.instancesChanged && !r.geometryChanged && r.blasRefitted == 0);

	// refit 次数上限：第 maxBLASRefits + 1 次几何变化改为完整重建，之后重新计数
	builder.GetSettings().maxBLASRefits = 3;   // 已 refit 2 次
	modelA->meshes[1].vertices[2].Position.y = 3;
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 1);
	modelA->meshes[1].vertices[2].Position.y = 3.5f;
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 0 && r.blasBuilt == 1);
	for (const auto& hint : builder.GetBLASHints())
		assert(hint.refitFrom == 0);   // 重建：硬件后端也完整 BUILD
	modelA->meshes[1].vertices[2].Position.y = 4;
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 1);
	builder.GetSettings().maxBLASRefits = 32;

	// 拓扑变化（索引变了）：不能 refit，完整重建
	std::swap(modelA->meshes[1].indices[0], modelA->meshes[1].indices[1]);
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 0 && r.blasBuilt == 1);

	// 关闭 refit：几何变化一律重建
	builder.GetSettings().blasRefit = false;
	modelA->meshes[1].vertices[2].Position.y = 5;
	SceneChanges::NotifyMeshGeometry(modelA->meshes[1]);
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 0 && r.blasBuilt == 1);
	builder.GetSettings().blasRefit = true;

	// 实例删除：引用 B 的实体没了 → 释放 B 的 BLAS
	scene.m_Registry.destroy(e3);
	r = builder.UpdateIncremental(scene, false);
	assert(r.geometryChanged && r.blasReleased == 1 && r.blasBuilt == 0);
	assert(builder.GetInstanceCount() == 4 && builder.GetStats().meshCount == 2);

	// 子网格停用：实例集合变化，BLAS 释放
	modelA->meshes[0].Active = false;
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasReleased == 1 && builder.GetInstanceCount() == 2);
	modelA->meshes[0].Active = true;
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasBuilt == 1 && builder.GetInstanceCount() == 4);

	// 切换 TLAS 构建算法：只重建 TLAS，BLAS 全部复用；切回来同样
	for (BVHTLASBuilder tlas : { BVHTLASBuilder::SAH, BVHTLASBuilder::LBVH })
	{
		builder.GetSettings().tlasBuilder = tlas;
		builder.InvalidateTLAS();
		r = builder.UpdateIncremental(scene, false);
		assert(r.instancesChanged && !r.geometryChanged && r.blasBuilt == 0 && builder.GetInstanceCount() == 4);
		r = builder.UpdateIncremental(scene, false);
		assert(!r.Changed());
	}

	// ---- 漏标校验 ----
	PTDirtyValidator validator;
	assert(validator.Check(scene).missedMask == 0);   // 基线

	// 正确通知的修改不报告
	e1.GetComponent<Component::Transform>().Position = { 0, 3, 0 };
	SceneChanges::NotifyTransform(scene.m_Registry, e1);
	assert(validator.Check(scene).missedMask == 0);

	// 故意漏掉通知：逐类别报告，并自愈（补发通知）
	v = SceneChanges::Snapshot();
	e2.GetComponent<Component::Transform>().Rotation = { 0, 1, 0 };
	auto report = validator.Check(scene);
	assert(report.missedMask == Bit(Category::InstanceTransform) && report.messages.size() == 1);
	assert(report.messages[0].find("A2") != std::string::npos);
	assert(ChangedSince(v) & Bit(Category::InstanceTransform));

	const uint64_t meshRevision = modelA->meshes[0].GeometryRevision;
	modelA->meshes[0].vertices[0].Normal = { 0, 0, 1 };   // 只改内容，不改数量 / 地址
	report = validator.Check(scene);
	assert(report.missedMask == Bit(Category::MeshGeometry));
	assert(modelA->meshes[0].GeometryRevision == meshRevision + 1);   // 自愈后 BLAS 会重建
	r = builder.UpdateIncremental(scene, false);
	assert(r.blasRefitted == 1);   // 只改法线：拓扑不变，refit

	red->GetParameter("u_BaseColor")->value = glm::vec4(0, 1, 0, 1);   // 绕过 setter
	report = validator.Check(scene);
	assert(report.missedMask == Bit(Category::MaterialParameters));

	e2.GetComponent<Component::MeshRender>().materials = { blue };
	lamp.GetComponent<Component::Light>().Intensity = 9;
	report = validator.Check(scene);
	assert(report.missedMask == (Bit(Category::MaterialAssignment) | Bit(Category::Lights)));

	lamp.GetComponent<Component::Tag>().Active = false;
	report = validator.Check(scene);
	assert(report.missedMask == Bit(Category::Lights));   // 灯光启用状态（实例集合里没有 lamp 的 MeshRender）

	assert(validator.Check(scene).missedMask == 0);       // 自愈后基线已更新
	assert(validator.GetMissedTotal() >= 6);

	std::cout << "PT dirty tests passed\n";
	return 0;
}
