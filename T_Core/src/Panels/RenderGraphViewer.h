#pragma once
#include "Core/Core.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class RenderGraph;

// Read-only inspector for a compiled RenderGraph: a pass dependency graph
// (passes in topological layers, edges labelled with the resource and hazard
// that force the ordering) and a resource lifetime timeline.
//
// Holds only layout/UI state. Everything it shows is read from the graph each
// frame, so a Reset()/Compile() between frames is picked up automatically.
class T_API RenderGraphViewer
{
public:
	void Draw(const RenderGraph& graph, bool* open);

private:
	struct Node
	{
		uint32_t pass = 0;
		int execIndex = -1;
		int layer = 0;
		int row = 0;
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // graph space
	};

	// All RGEdgeInfo entries between one ordered pair of passes, drawn as one curve.
	struct EdgeGroup
	{
		uint32_t from = 0;
		uint32_t to = 0;
		std::vector<uint32_t> infos;   // indices into RenderGraph::GetEdgeInfos()
	};

	void RebuildLayout(const RenderGraph& graph);
	void FitToView(float canvasW, float canvasH);
	void UpdateBounds();

	void DrawGraphTab(const RenderGraph& graph);
	void DrawPassDetails(const RenderGraph& graph);
	void DrawLifetimeTab(const RenderGraph& graph);

	bool HazardVisible(int hazard) const;
	static bool MatchesSearch(const std::string& name, const char* query);

	// Layout cache, keyed on what can change the graph's shape.
	uint32_t m_LayoutGeneration = 0;
	uint32_t m_LayoutCompileSerial = UINT32_MAX;
	size_t m_LayoutPassCount = 0;
	size_t m_LayoutEdgeCount = 0;
	bool m_LayoutShowCulled = true;

	std::vector<Node> m_Nodes;
	std::vector<int> m_NodeOfPass;          // pass id -> index in m_Nodes, -1 if hidden
	std::vector<EdgeGroup> m_Edges;
	float m_BoundsX = 0.0f, m_BoundsY = 0.0f;   // graph-space min corner
	float m_BoundsW = 0.0f, m_BoundsH = 0.0f;

	// Node dragging. Positions the user placed by hand are kept by pass name
	// (graph-space x, y) so they survive Reset()/Compile() rebuilds.
	int m_DraggingNode = -1;
	bool m_Panning = false;
	std::unordered_map<std::string, std::pair<float, float>> m_ManualPos;

	// View state.
	float m_PanX = 0.0f, m_PanY = 0.0f;
	float m_Zoom = 1.0f;
	bool m_FitRequested = true;

	// Selection survives a rebuild only by pass name: ids are not stable.
	uint32_t m_SelectedPass = UINT32_MAX;
	std::string m_SelectedName;

	// Filters.
	bool m_ShowRAW = true;
	bool m_ShowWAR = true;
	bool m_ShowWAW = true;
	bool m_ShowCulled = true;
	char m_Search[64] = {};

	// Lifetime tab.
	int m_LifetimeSort = 0;                 // 0 = first use, 1 = name
	bool m_HideUnused = true;
	char m_ResourceSearch[64] = {};
};
