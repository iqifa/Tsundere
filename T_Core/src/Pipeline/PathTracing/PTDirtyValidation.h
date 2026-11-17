#pragma once

// ---------------------------------------------------------------------------
// PTDirtyValidator — 开发期“数据变了但没有通知”检查（ENABLE_PT_DIRTY_VALIDATION）
//
// 正常运行时 PathTraceScene 完全依赖 SceneChanges 的版本号，不扫描场景内容。
// 开发构建里，PathTraceScene 在消费版本号之前调用 Check()：
//   * 对每个对象计算内容哈希（实例变换、实例集合、材质分配、Mesh 顶点 / 索引内容、
//     材质参数、灯光），和上一次检查时的哈希比较；
//   * 内容变了、而对应的通知没有发生（类别版本号 / Mesh::GeometryRevision /
//     Material::GetRevision 自上次检查以来没变），就是漏标：逐对象报告，
//     并补发通知（自愈），让本帧的更新照常进行。
// 检查以“上一次检查”为基准，所以间隔多帧检查也不会漏判：期间只要发过通知就算覆盖。
//
// 本类始终参与编译（便于测试），是否调用由 ENABLE_PT_DIRTY_VALIDATION 决定。
// ---------------------------------------------------------------------------

#include "Scene/SceneChanges.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Scene;
namespace Engine { class Material; }

class T_API PTDirtyValidator
{
public:
	struct Report
	{
		uint32_t missedMask = 0;              // 漏标的类别
		std::vector<std::string> messages;    // 逐对象描述
	};

	// 返回漏标报告；已为漏标对象补发通知。首次调用只建立基线。
	Report Check(const Scene& scene);
	void Reset() { m_HasBaseline = false; }

	uint64_t GetMissedTotal() const { return m_MissedTotal; }
	const std::string& GetLastReport() const { return m_LastReport; }

private:
	struct MeshRecord
	{
		uint64_t revision = 0;
		uint64_t hash = 0;
		std::string label;
	};
	struct MaterialRecord
	{
		uint64_t revision = 0;
		uint64_t hash = 0;
	};
	struct Baseline
	{
		std::unordered_map<uint32_t, uint64_t> transforms;    // 实体 → 变换哈希（MeshRender 实体）
		std::unordered_map<uint32_t, uint64_t> instanceSets;  // 实体 → 启用 / 模型 / 子网格状态
		std::unordered_map<uint32_t, uint64_t> assignments;   // 实体 → 材质指针序列
		std::unordered_map<uint32_t, uint64_t> lights;        // 实体 → 灯光内容 + 位置 + 启用
		std::unordered_map<uint64_t, MeshRecord> meshes;      // GeometryId → 内容
		std::unordered_map<const Engine::Material*, MaterialRecord> materials;
		SceneChanges::Versions versions;
	};

	Baseline Capture(const Scene& scene) const;

	Baseline m_Baseline;
	bool m_HasBaseline = false;
	uint64_t m_MissedTotal = 0;
	std::string m_LastReport;
};
