#include "ExternalFiles.h"
#include "Panels/RenderGraphViewer.h"
#include "Pipeline/RenderGraphPass.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <unordered_map>
#include <set>
#include <tuple>

namespace
{
	// Palette tuned for the editor's dark theme.
	const ImU32 kHazardColor[3] = {
		IM_COL32(90, 160, 255, 255),    // RAW
		IM_COL32(255, 170, 60, 255),    // WAR
		IM_COL32(240, 80, 80, 255),     // WAW
	};
	const ImU32 kNodeBody      = IM_COL32(40, 43, 50, 245);
	const ImU32 kNodeRaster    = IM_COL32(55, 90, 140, 255);
	const ImU32 kNodeCompute   = IM_COL32(105, 70, 140, 255);
	const ImU32 kNodeCulled    = IM_COL32(70, 70, 70, 255);
	const ImU32 kNodeBorder    = IM_COL32(20, 20, 20, 255);
	const ImU32 kNodeSelected  = IM_COL32(255, 220, 90, 255);
	const ImU32 kNodeMatch     = IM_COL32(120, 230, 140, 255);
	const ImU32 kBarrierBadge  = IM_COL32(240, 200, 60, 255);
	const ImU32 kText          = IM_COL32(230, 230, 230, 255);
	const ImU32 kTextDim       = IM_COL32(160, 160, 160, 255);
	const ImU32 kCanvasBg      = IM_COL32(28, 30, 34, 255);
	const ImU32 kGridLine      = IM_COL32(45, 48, 54, 255);

	const float kNodeMinWidth = 150.0f;
	const float kNodePad = 8.0f;
	const float kLayerGap = 90.0f;
	const float kRowGap = 26.0f;

	const char* HazardName(RGHazard h)
	{
		switch (h)
		{
		case RGHazard::RAW: return "RAW";
		case RGHazard::WAR: return "WAR";
		case RGHazard::WAW: return "WAW";
		}
		return "?";
	}

	const char* AccessName(RGAccess a)
	{
		switch (a)
		{
		case RGAccess::ReadSRV:      return "ReadSRV";
		case RGAccess::WriteUAV:     return "WriteUAV";
		case RGAccess::RenderTarget: return "RenderTarget";
		case RGAccess::DepthRead:    return "DepthRead";
		case RGAccess::DepthWrite:   return "DepthWrite";
		case RGAccess::CopySrc:      return "CopySrc";
		case RGAccess::CopyDst:      return "CopyDst";
		case RGAccess::Present:      return "Present";
		}
		return "?";
	}

	const char* FormatName(Format f)
	{
		switch (f)
		{
		case Format::Unknown:            return "Unknown";
		case Format::R8_UNORM:           return "R8";
		case Format::RGBA8_UNORM:        return "RGBA8";
		case Format::RGBA8_SRGB:         return "RGBA8_SRGB";
		case Format::R16F:               return "R16F";
		case Format::RG16F:              return "RG16F";
		case Format::RGBA16F:            return "RGBA16F";
		case Format::RGBA32F:            return "RGBA32F";
		case Format::D24_UNORM_S8_UINT:  return "D24S8";
		case Format::D32_SFLOAT:         return "D32F";
		case Format::D32_SFLOAT_S8_UINT: return "D32FS8";
		case Format::R32_UINT:           return "R32UI";
		case Format::R32_SINT:           return "R32I";
		}
		return "?";
	}

	std::string BarrierString(BarrierFlags b)
	{
		std::string s;
		auto add = [&](BarrierFlags f, const char* name)
		{
			if (b & f)
			{
				if (!s.empty()) s += " | ";
				s += name;
			}
		};
		add(BarrierFlags::ShaderImage, "ShaderImage");
		add(BarrierFlags::TextureFetch, "TextureFetch");
		add(BarrierFlags::StorageBuffer, "StorageBuffer");
		add(BarrierFlags::Framebuffer, "Framebuffer");
		return s.empty() ? std::string("None") : s;
	}

	std::string ResourceDesc(const RGResource& r)
	{
		char buf[96];
		if (r.type == RGResourceType::Texture)
		{
			const RDGTextureDesc& d = r.textureDesc;
			int n = snprintf(buf, sizeof(buf), "%ux%u %s", d.width, d.height, FormatName(d.format));
			if (d.sampleCount > 1 && n > 0 && n < (int)sizeof(buf))
				n += snprintf(buf + n, sizeof(buf) - n, " x%uMSAA", d.sampleCount);
			if (d.mipLevel > 1 && n > 0 && n < (int)sizeof(buf))
				snprintf(buf + n, sizeof(buf) - n, " mips:%u", d.mipLevel);
		}
		else
		{
			snprintf(buf, sizeof(buf), "%u bytes", r.bufferDesc.size);
		}
		return buf;
	}

	bool IsComputePass(const RGPass& p)
	{
		return p.colorAttachments.empty() && !p.depthAttachment.has_value();
	}

	std::string ToLower(std::string s)
	{
		for (char& c : s)
			c = (char)std::tolower((unsigned char)c);
		return s;
	}

	const std::string& ResourceName(const RenderGraph& g, ResourceId id)
	{
		static const std::string invalid = "<invalid>";
		const auto& res = g.GetResources();
		return id < res.size() ? res[id].name : invalid;
	}

	const std::string& PassName(const RenderGraph& g, PassId id)
	{
		static const std::string invalid = "<invalid>";
		const auto& passes = g.GetPasses();
		return id < passes.size() ? passes[id].name : invalid;
	}

    std::string DependencyUses(const RGPass& pass, ResourceId resource, bool reads)
    {
        std::string result;
        std::set<RGAccess> seen;
        for (const auto& a : reads ? pass.reads : pass.writes)
        {
            if (a.resource != resource || !seen.insert(a.access).second) continue;
            if (!result.empty()) result += " + ";
            switch (a.access)
            {
            case RGAccess::ReadSRV: result += "Shader sample/read"; break;
            case RGAccess::CopySrc: result += "Copy source"; break;
            case RGAccess::CopyDst: result += "Copy destination"; break;
            case RGAccess::DepthRead: result += "Read-only depth test"; break;
            case RGAccess::DepthWrite: result += "Depth write"; break;
            case RGAccess::RenderTarget: result += "Color attachment write"; break;
            default: result += AccessName(a.access); break;
            }
        }
        return result;
    }

    void DrawDependencyUses(const RenderGraph& graph, const RGEdgeInfo& edge)
    {
        const auto& passes = graph.GetPasses();
        const auto source = DependencyUses(passes[edge.from], edge.resource, edge.hazard == RGHazard::WAR);
        const auto destination = DependencyUses(passes[edge.to], edge.resource, edge.hazard == RGHazard::RAW);
        ImGui::TextDisabled("  Source: %s", source.c_str());
        ImGui::TextDisabled("  Destination: %s", destination.c_str());
    }

    void DrawShaderAccesses(const RenderGraph& graph, const RGPass& pass)
    {
        bool any = false;
        for (const auto* accesses : { &pass.reads, &pass.writes })
            for (const auto& a : *accesses)
                if (a.access != RGAccess::CopySrc && a.access != RGAccess::CopyDst &&
                    a.access != RGAccess::RenderTarget && a.access != RGAccess::DepthRead &&
                    a.access != RGAccess::DepthWrite)
                {
                    ImGui::BulletText("%s [%s]", ResourceName(graph, a.resource).c_str(), AccessName(a.access));
                    any = true;
                }
        if (!any) ImGui::TextDisabled("none");
    }

    const char* LoadName(RGLoadOp op)
    {
        return op == RGLoadOp::Load ? "Load: preserve existing" :
            op == RGLoadOp::Clear ? "Clear" : "Discard";
    }

    void DrawAttachments(const RenderGraph& graph, const RGPass& pass)
    {
        for (const auto& c : pass.colorAttachments)
        {
            ImGui::BulletText("Color[%u]: %s", c.slot, ResourceName(graph, c.texture.id).c_str());
            ImGui::TextDisabled("  %s | Store: %s", LoadName(c.loadOp),
                c.storeOp == RGStoreOp::Store ? "keep" : "discard");
            if (c.resolveTarget.id != InvalidResourceId)
                ImGui::Text("  MSAA resolve -> %s", ResourceName(graph, c.resolveTarget.id).c_str());
        }
        if (pass.depthAttachment)
        {
            const auto& d = *pass.depthAttachment;
            ImGui::BulletText("Depth: %s", ResourceName(graph, d.texture.id).c_str());
            ImGui::TextDisabled("  %s | %s", d.readOnly ? "Read-only depth test" : "Depth write", LoadName(d.loadOp));
            if (d.resolveTarget.id != InvalidResourceId)
                ImGui::Text("  MSAA resolve -> %s", ResourceName(graph, d.resolveTarget.id).c_str());
        }
        if (pass.colorAttachments.empty() && !pass.depthAttachment) ImGui::TextDisabled("none");
    }

    void DrawCopies(const RenderGraph& graph, const RGPass& pass)
    {
        for (bool after : {false, true})
        {
            bool header = false;
            for (const auto& copy : pass.textureCopies)
            {
                if (copy.afterRender != after) continue;
                if (!header) ImGui::TextDisabled("%s draw", after ? "After" : "Before");
                header = true;
                ImGui::BulletText("%s [read]", ResourceName(graph, copy.source.id).c_str());
                ImGui::Text("    -> %s [write]", ResourceName(graph, copy.destination.id).c_str());
            }
        }
        // Do not guess pairings for declarations without explicit metadata.
        bool unpaired = false;
        for (const auto* accesses : { &pass.reads, &pass.writes })
            for (const auto& a : *accesses)
            {
                if (a.access != RGAccess::CopySrc && a.access != RGAccess::CopyDst) continue;
                bool paired = false;
                for (const auto& copy : pass.textureCopies)
                    paired |= a.resource == (a.access == RGAccess::CopySrc ? copy.source.id : copy.destination.id);
                if (!paired) {
                    if (!unpaired) ImGui::TextDisabled("Unpaired copy declarations");
                    unpaired = true;
                    ImGui::BulletText("%s [%s]", ResourceName(graph, a.resource).c_str(), AccessName(a.access));
                }
            }
        if (pass.textureCopies.empty() && !unpaired) ImGui::TextDisabled("none");
    }

	ImU32 WithAlpha(ImU32 c, float a)
	{
		const ImU32 alpha = (ImU32)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
		return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
	}
}

bool RenderGraphViewer::HazardVisible(int hazard) const
{
	switch ((RGHazard)hazard)
	{
	case RGHazard::RAW: return m_ShowRAW;
	case RGHazard::WAR: return m_ShowWAR;
	case RGHazard::WAW: return m_ShowWAW;
	}
	return true;
}

bool RenderGraphViewer::MatchesSearch(const std::string& name, const char* query)
{
	if (!query || query[0] == '\0')
		return false;
	return ToLower(name).find(ToLower(query)) != std::string::npos;
}

void RenderGraphViewer::Draw(const RenderGraph& graph, bool* open)
{
	ImGui::SetNextWindowSize(ImVec2(960.0f, 620.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Render Graph", open))
	{
		ImGui::End();
		return;
	}

	const auto& passes = graph.GetPasses();
	if (passes.empty() || graph.GetExecutionOrder().empty())
	{
		ImGui::TextDisabled("Render graph has not been compiled.");
		ImGui::End();
		return;
	}

	if (graph.GetGeneration() != m_LayoutGeneration
		|| graph.GetCompileSerial() != m_LayoutCompileSerial
		|| passes.size() != m_LayoutPassCount
		|| graph.GetEdgeInfos().size() != m_LayoutEdgeCount
		|| m_ShowCulled != m_LayoutShowCulled)
	{
		RebuildLayout(graph);
	}

	size_t culled = 0;
	for (const auto& p : passes)
		culled += p.culled ? 1 : 0;
	ImGui::Text("Passes: %zu (culled %zu)   Resources: %zu   Dependencies: %zu",
		passes.size(), culled, graph.GetResources().size(), graph.GetEdgeInfos().size());

	if (ImGui::BeginTabBar("##RenderGraphTabs"))
	{
		if (ImGui::BeginTabItem("Dependency Graph"))
		{
			DrawGraphTab(graph);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Resource Lifetimes"))
		{
			DrawLifetimeTab(graph);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

void RenderGraphViewer::RebuildLayout(const RenderGraph& graph)
{
	const auto& passes = graph.GetPasses();
	const auto& order = graph.GetExecutionOrder();
	const auto& infos = graph.GetEdgeInfos();

	m_LayoutGeneration = graph.GetGeneration();
	m_LayoutCompileSerial = graph.GetCompileSerial();
	m_LayoutPassCount = passes.size();
	m_LayoutEdgeCount = infos.size();
	m_LayoutShowCulled = m_ShowCulled;

	// Re-resolve the selection by name; pass ids change across rebuilds.
	m_SelectedPass = UINT32_MAX;
	if (!m_SelectedName.empty())
	{
		for (PassId i = 0; i < passes.size(); ++i)
		{
			if (passes[i].name == m_SelectedName)
			{
				m_SelectedPass = i;
				break;
			}
		}
	}

	// Group per-resource edge records into one curve per pass pair.
	m_Edges.clear();
	std::unordered_map<uint64_t, size_t> groupOf;
	for (uint32_t i = 0; i < infos.size(); ++i)
	{
		const RGEdgeInfo& e = infos[i];
		const uint64_t key = ((uint64_t)e.from << 32) | e.to;
		auto it = groupOf.find(key);
		if (it == groupOf.end())
		{
			it = groupOf.emplace(key, m_Edges.size()).first;
			m_Edges.push_back({ e.from, e.to, {} });
		}
		m_Edges[it->second].infos.push_back(i);
	}

	std::vector<int> execIndexOf(passes.size(), -1);
	for (size_t i = 0; i < order.size(); ++i)
		if (order[i] < passes.size())
			execIndexOf[order[i]] = (int)i;

	// Longest-path layering. executionOrder_ is topological, so one sweep in
	// that order settles every layer (a cycle falls back to declaration order
	// in the graph; the sweep still terminates, it just lays out less neatly).
	std::vector<std::vector<uint32_t>> successors(passes.size());
	std::vector<std::vector<uint32_t>> predecessors(passes.size());
	for (const EdgeGroup& g : m_Edges)
	{
		successors[g.from].push_back(g.to);
		predecessors[g.to].push_back(g.from);
	}

	std::vector<int> layer(passes.size(), 0);
	int maxLayer = 0;
	for (PassId p : order)
	{
		if (p >= passes.size() || passes[p].culled)
			continue;
		for (uint32_t next : successors[p])
			layer[next] = std::max(layer[next], layer[p] + 1);
		maxLayer = std::max(maxLayer, layer[p]);
	}

	// Culled passes have no declarations; give them their own last column.
	const int culledLayer = maxLayer + 1;
	std::vector<std::vector<uint32_t>> layers(culledLayer + 1);
	for (PassId p : order)
	{
		if (p >= passes.size())
			continue;
		if (passes[p].culled)
		{
			if (m_ShowCulled)
				layers[culledLayer].push_back(p);
			continue;
		}
		layers[layer[p]].push_back(p);
	}
	while (!layers.empty() && layers.back().empty())
		layers.pop_back();

	// One barycenter sweep left-to-right to cut down edge crossings.
	std::vector<float> rowOf(passes.size(), 0.0f);
	for (size_t L = 0; L < layers.size(); ++L)
	{
		auto& nodes = layers[L];
		if (L > 0)
		{
			std::vector<float> key(passes.size(), 0.0f);
			for (uint32_t p : nodes)
			{
				float sum = 0.0f;
				int count = 0;
				for (uint32_t pred : predecessors[p])
				{
					sum += rowOf[pred];
					++count;
				}
				key[p] = count ? sum / (float)count : (float)execIndexOf[p];
			}
			std::stable_sort(nodes.begin(), nodes.end(), [&](uint32_t a, uint32_t b)
			{
				if (key[a] != key[b]) return key[a] < key[b];
				return execIndexOf[a] < execIndexOf[b];
			});
		}
		for (size_t r = 0; r < nodes.size(); ++r)
			rowOf[nodes[r]] = (float)r;
	}

	// Sizes measured at the base font size; drawing scales them with zoom.
	const float lineH = ImGui::GetFontSize();
	const float nodeH = lineH * 2.0f + kNodePad * 2.5f;

	size_t maxRows = 0;
	for (const auto& nodes : layers)
		maxRows = std::max(maxRows, nodes.size());
	const float fullH = maxRows > 0 ? (float)maxRows * nodeH + (float)(maxRows - 1) * kRowGap : 0.0f;

	m_Nodes.clear();
	m_NodeOfPass.assign(passes.size(), -1);
	float x = 0.0f;
	for (size_t L = 0; L < layers.size(); ++L)
	{
		const auto& nodes = layers[L];
		float colW = kNodeMinWidth;
		for (uint32_t p : nodes)
		{
			char title[160];
			snprintf(title, sizeof(title), "#%d %s", execIndexOf[p], passes[p].name.c_str());
			colW = std::max(colW, ImGui::CalcTextSize(title).x + kNodePad * 2.0f + 14.0f);
		}

		const float colH = nodes.empty() ? 0.0f
			: (float)nodes.size() * nodeH + (float)(nodes.size() - 1) * kRowGap;
		float y = (fullH - colH) * 0.5f;
		for (size_t r = 0; r < nodes.size(); ++r)
		{
			Node n;
			n.pass = nodes[r];
			n.execIndex = execIndexOf[n.pass];
			n.layer = (int)L;
			n.row = (int)r;
			n.x = x;
			n.y = y;
			n.w = colW;
			n.h = nodeH;
			m_NodeOfPass[n.pass] = (int)m_Nodes.size();
			m_Nodes.push_back(n);
			y += nodeH + kRowGap;
		}
		x += colW + kLayerGap;
	}

	// Hand-placed positions win over the automatic layout.
	for (Node& n : m_Nodes)
	{
		auto it = m_ManualPos.find(passes[n.pass].name);
		if (it != m_ManualPos.end())
		{
			n.x = it->second.first;
			n.y = it->second.second;
		}
	}

	m_DraggingNode = -1;
	UpdateBounds();
	m_FitRequested = true;
}

void RenderGraphViewer::UpdateBounds()
{
	if (m_Nodes.empty())
	{
		m_BoundsX = m_BoundsY = m_BoundsW = m_BoundsH = 0.0f;
		return;
	}
	float minX = m_Nodes[0].x, minY = m_Nodes[0].y;
	float maxX = m_Nodes[0].x + m_Nodes[0].w, maxY = m_Nodes[0].y + m_Nodes[0].h;
	for (const Node& n : m_Nodes)
	{
		minX = std::min(minX, n.x);
		minY = std::min(minY, n.y);
		maxX = std::max(maxX, n.x + n.w);
		maxY = std::max(maxY, n.y + n.h);
	}
	m_BoundsX = minX;
	m_BoundsY = minY;
	m_BoundsW = maxX - minX;
	m_BoundsH = maxY - minY;
}

void RenderGraphViewer::FitToView(float canvasW, float canvasH)
{
	const float margin = 30.0f;
	UpdateBounds();
	if (m_BoundsW <= 0.0f || m_BoundsH <= 0.0f || canvasW <= margin * 2.0f || canvasH <= margin * 2.0f)
	{
		m_Zoom = 1.0f;
		m_PanX = margin - m_BoundsX;
		m_PanY = margin - m_BoundsY;
		return;
	}
	m_Zoom = std::min((canvasW - margin * 2.0f) / m_BoundsW, (canvasH - margin * 2.0f) / m_BoundsH);
	m_Zoom = std::clamp(m_Zoom, 0.2f, 1.5f);
	m_PanX = (canvasW - m_BoundsW * m_Zoom) * 0.5f - m_BoundsX * m_Zoom;
	m_PanY = (canvasH - m_BoundsH * m_Zoom) * 0.5f - m_BoundsY * m_Zoom;
}

void RenderGraphViewer::DrawGraphTab(const RenderGraph& graph)
{
	const auto& passes = graph.GetPasses();
	const auto& infos = graph.GetEdgeInfos();

	// ---- Toolbar ----------------------------------------------------------
	const char* hazardLabels[3] = { "RAW", "WAR", "WAW" };
	bool* hazardToggles[3] = { &m_ShowRAW, &m_ShowWAR, &m_ShowWAW };
	for (int h = 0; h < 3; ++h)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kHazardColor[h]));
		ImGui::Checkbox(hazardLabels[h], hazardToggles[h]);
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	ImGui::Checkbox("Show culled", &m_ShowCulled);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputTextWithHint("##search", "Search pass...", m_Search, sizeof(m_Search));
	ImGui::SameLine();
	if (ImGui::Button("Fit"))
		m_FitRequested = true;
	ImGui::SameLine();
	ImGui::BeginDisabled(m_ManualPos.empty());
	if (ImGui::Button("Reset layout"))
	{
		m_ManualPos.clear();
		m_LayoutCompileSerial = UINT32_MAX;   // forces RebuildLayout next frame
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Left drag a pass: move it (kept across rebuilds)\n"
			"Left drag empty space / right / middle drag: pan\nWheel: zoom\n"
			"Click a pass to select it, click empty space to deselect\n"
			"Edges point from producer to consumer; colour = hazard type");

	// ---- Canvas -------------------------------------------------------------
	const bool hasSelection = m_SelectedPass < passes.size();
	const float detailsH = hasSelection ? 190.0f : 0.0f;
	ImVec2 avail = ImGui::GetContentRegionAvail();
	avail.y = std::max(120.0f, avail.y - detailsH);

	ImGui::BeginChild("##RGCanvas", avail, ImGuiChildFlags_Borders,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const ImVec2 size = ImGui::GetContentRegionAvail();
	if (size.x < 1.0f || size.y < 1.0f)
	{
		ImGui::EndChild();
		return;
	}

	if (m_FitRequested)
	{
		FitToView(size.x, size.y);
		m_FitRequested = false;
	}

	ImGui::InvisibleButton("##canvas", size,
		ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	const bool canvasHovered = ImGui::IsItemHovered();
	const bool canvasActive = ImGui::IsItemActive();
	ImGuiIO& io = ImGui::GetIO();

	const bool canvasActivated = ImGui::IsItemActivated();

	if (canvasActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)))
	{
		m_PanX += io.MouseDelta.x;
		m_PanY += io.MouseDelta.y;
	}

	if (canvasHovered && io.MouseWheel != 0.0f)
	{
		const float newZoom = std::clamp(m_Zoom * std::pow(1.15f, io.MouseWheel), 0.15f, 3.0f);
		const float mx = io.MousePos.x - origin.x;
		const float my = io.MousePos.y - origin.y;
		m_PanX = mx - (mx - m_PanX) * (newZoom / m_Zoom);
		m_PanY = my - (my - m_PanY) * (newZoom / m_Zoom);
		m_Zoom = newZoom;
	}

	auto toScreen = [&](float gx, float gy)
	{
		return ImVec2(origin.x + m_PanX + gx * m_Zoom, origin.y + m_PanY + gy * m_Zoom);
	};

	// Hit-test nodes. A node being dragged stays "hovered" even if the cursor
	// briefly outruns it.
	if (m_DraggingNode >= (int)m_Nodes.size())
		m_DraggingNode = -1;
	int hoveredNode = m_DraggingNode;
	if (hoveredNode < 0 && canvasHovered)
	{
		for (int i = (int)m_Nodes.size() - 1; i >= 0; --i)
		{
			const Node& n = m_Nodes[i];
			const ImVec2 a = toScreen(n.x, n.y);
			const ImVec2 b = toScreen(n.x + n.w, n.y + n.h);
			if (io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y)
			{
				hoveredNode = i;
				break;
			}
		}
	}

	// Left button: press on a pass selects it and starts dragging it; press on
	// empty canvas pans, and a click there without dragging clears selection.
	if (canvasActivated && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		if (hoveredNode >= 0)
		{
			m_DraggingNode = hoveredNode;
			m_SelectedPass = m_Nodes[hoveredNode].pass;
			m_SelectedName = passes[m_SelectedPass].name;
		}
		else
		{
			m_Panning = true;
		}
	}

	if (canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
	{
		if (m_DraggingNode >= 0)
		{
			Node& n = m_Nodes[m_DraggingNode];
			n.x += io.MouseDelta.x / m_Zoom;
			n.y += io.MouseDelta.y / m_Zoom;
			m_ManualPos[passes[n.pass].name] = { n.x, n.y };
		}
		else if (m_Panning)
		{
			m_PanX += io.MouseDelta.x;
			m_PanY += io.MouseDelta.y;
		}
	}

	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		if (m_Panning && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
		{
			m_SelectedPass = UINT32_MAX;
			m_SelectedName.clear();
		}
		m_DraggingNode = -1;
		m_Panning = false;
	}

	if (m_DraggingNode >= 0)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	else if (hoveredNode >= 0)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	// Focus = hovered pass, else selected pass. Its direct neighbours stay lit.
	const uint32_t focusPass = hoveredNode >= 0 ? m_Nodes[hoveredNode].pass
		: (m_SelectedPass < passes.size() ? m_SelectedPass : UINT32_MAX);
	std::vector<char> lit(passes.size(), focusPass == UINT32_MAX ? 1 : 0);
	if (focusPass != UINT32_MAX)
	{
		lit[focusPass] = 1;
		for (const EdgeGroup& g : m_Edges)
		{
			if (g.from == focusPass) lit[g.to] = 1;
			if (g.to == focusPass) lit[g.from] = 1;
		}
	}
	const bool searching = m_Search[0] != '\0';

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 canvasMax(origin.x + size.x, origin.y + size.y);
	dl->PushClipRect(origin, canvasMax, true);
	dl->AddRectFilled(origin, canvasMax, kCanvasBg);

	// Background grid.
	const float grid = 48.0f * m_Zoom;
	if (grid > 8.0f)
	{
		for (float gx = std::fmod(m_PanX, grid); gx < size.x; gx += grid)
			dl->AddLine(ImVec2(origin.x + gx, origin.y), ImVec2(origin.x + gx, canvasMax.y), kGridLine);
		for (float gy = std::fmod(m_PanY, grid); gy < size.y; gy += grid)
			dl->AddLine(ImVec2(origin.x, origin.y + gy), ImVec2(canvasMax.x, origin.y + gy), kGridLine);
	}

	// ---- Edges --------------------------------------------------------------
	int hoveredEdge = -1;
	float hoveredEdgeDist = 6.0f;
	struct EdgeDraw { ImVec2 p0, p1, p2, p3; ImU32 color; float thickness; };
	std::vector<EdgeDraw> edgeDraws(m_Edges.size());
	std::vector<char> edgeVisible(m_Edges.size(), 0);

	for (size_t e = 0; e < m_Edges.size(); ++e)
	{
		const EdgeGroup& g = m_Edges[e];
		const int fromNode = g.from < m_NodeOfPass.size() ? m_NodeOfPass[g.from] : -1;
		const int toNode = g.to < m_NodeOfPass.size() ? m_NodeOfPass[g.to] : -1;
		if (fromNode < 0 || toNode < 0)
			continue;

		// Colour by the most significant visible hazard: RAW (true data flow)
		// beats WAW, which beats WAR (pure ordering).
		int best = -1;
		for (uint32_t idx : g.infos)
		{
			const int h = (int)infos[idx].hazard;
			if (!HazardVisible(h))
				continue;
			const int rank = h == (int)RGHazard::RAW ? 0 : (h == (int)RGHazard::WAW ? 1 : 2);
			const int bestRank = best < 0 ? 99 : (best == (int)RGHazard::RAW ? 0 : (best == (int)RGHazard::WAW ? 1 : 2));
			if (rank < bestRank)
				best = h;
		}
		if (best < 0)
			continue;

		const Node& a = m_Nodes[fromNode];
		const Node& b = m_Nodes[toNode];
		EdgeDraw d;
		d.p0 = toScreen(a.x + a.w, a.y + a.h * 0.5f);
		d.p3 = toScreen(b.x, b.y + b.h * 0.5f);
		const float dx = std::max(40.0f * m_Zoom, std::fabs(d.p3.x - d.p0.x) * 0.5f);
		d.p1 = ImVec2(d.p0.x + dx, d.p0.y);
		d.p2 = ImVec2(d.p3.x - dx, d.p3.y);

		const bool edgeLit = focusPass == UINT32_MAX || g.from == focusPass || g.to == focusPass;
		d.color = edgeLit ? kHazardColor[best] : WithAlpha(kHazardColor[best], 0.15f);
		d.thickness = (edgeLit && focusPass != UINT32_MAX ? 2.5f : 1.6f) * std::max(0.6f, m_Zoom);
		edgeDraws[e] = d;
		edgeVisible[e] = 1;

		if (canvasHovered && hoveredNode < 0)
		{
			const ImVec2 c = ImBezierCubicClosestPointCasteljau(d.p0, d.p1, d.p2, d.p3, io.MousePos, 1.0f);
			const float dist = std::sqrt((c.x - io.MousePos.x) * (c.x - io.MousePos.x) + (c.y - io.MousePos.y) * (c.y - io.MousePos.y));
			if (dist < hoveredEdgeDist)
			{
				hoveredEdgeDist = dist;
				hoveredEdge = (int)e;
			}
		}
	}

	for (size_t e = 0; e < m_Edges.size(); ++e)
	{
		if (!edgeVisible[e])
			continue;
		const EdgeDraw& d = edgeDraws[e];
		const float t = (int)e == hoveredEdge ? d.thickness + 2.0f : d.thickness;
		dl->AddBezierCubic(d.p0, d.p1, d.p2, d.p3, d.color, t);

		// Arrow head at the consumer end.
		const float ah = 7.0f * std::max(0.6f, m_Zoom);
		dl->AddTriangleFilled(
			ImVec2(d.p3.x, d.p3.y),
			ImVec2(d.p3.x - ah, d.p3.y - ah * 0.6f),
			ImVec2(d.p3.x - ah, d.p3.y + ah * 0.6f),
			d.color);

		// Resource count label where several resources share one curve.
		if (m_Edges[e].infos.size() > 1 && m_Zoom > 0.45f)
		{
			const ImVec2 mid = ImBezierCubicCalc(d.p0, d.p1, d.p2, d.p3, 0.5f);
			char label[16];
			snprintf(label, sizeof(label), "%zu", m_Edges[e].infos.size());
			const ImVec2 ts = ImGui::CalcTextSize(label);
			dl->AddRectFilled(ImVec2(mid.x - ts.x * 0.5f - 3, mid.y - ts.y * 0.5f - 1),
				ImVec2(mid.x + ts.x * 0.5f + 3, mid.y + ts.y * 0.5f + 1), kCanvasBg, 3.0f);
			dl->AddText(ImVec2(mid.x - ts.x * 0.5f, mid.y - ts.y * 0.5f), d.color, label);
		}
	}

	// ---- Nodes --------------------------------------------------------------
	ImFont* font = ImGui::GetFont();
	const float fontSize = ImGui::GetFontSize() * m_Zoom;
	const bool drawText = m_Zoom > 0.35f;
	const float rounding = 5.0f * m_Zoom;

	for (int i = 0; i < (int)m_Nodes.size(); ++i)
	{
		const Node& n = m_Nodes[i];
		const RGPass& pass = passes[n.pass];
		const ImVec2 a = toScreen(n.x, n.y);
		const ImVec2 b = toScreen(n.x + n.w, n.y + n.h);
		if (b.x < origin.x || a.x > canvasMax.x || b.y < origin.y || a.y > canvasMax.y)
			continue;

		const bool dim = !lit[n.pass] || (searching && !MatchesSearch(pass.name, m_Search));
		const float alpha = dim ? 0.3f : 1.0f;
		const ImU32 header = pass.culled ? kNodeCulled : (IsComputePass(pass) ? kNodeCompute : kNodeRaster);
		const float headerH = (ImGui::GetFontSize() + kNodePad) * m_Zoom;

		dl->AddRectFilled(a, b, WithAlpha(kNodeBody, alpha), rounding);
		dl->AddRectFilled(a, ImVec2(b.x, a.y + headerH), WithAlpha(header, alpha), rounding, ImDrawFlags_RoundCornersTop);

		ImU32 border = kNodeBorder;
		float borderW = 1.0f;
		if (n.pass == m_SelectedPass) { border = kNodeSelected; borderW = 2.5f; }
		else if (searching && MatchesSearch(pass.name, m_Search)) { border = kNodeMatch; borderW = 2.0f; }
		else if (i == hoveredNode) { border = IM_COL32(200, 200, 200, 255); borderW = 1.5f; }
		dl->AddRect(a, b, WithAlpha(border, std::max(alpha, 0.6f)), rounding, 0, borderW);

		if (pass.barriers != BarrierFlags::None)
		{
			const float r = 4.5f * std::max(0.6f, m_Zoom);
			dl->AddCircleFilled(ImVec2(b.x - r - 4.0f * m_Zoom, a.y + headerH * 0.5f), r, WithAlpha(kBarrierBadge, alpha));
		}

		if (drawText)
		{
			char title[160];
			snprintf(title, sizeof(title), "#%d %s", n.execIndex, pass.name.c_str());
			dl->AddText(font, fontSize, ImVec2(a.x + kNodePad * m_Zoom, a.y + kNodePad * 0.5f * m_Zoom),
				WithAlpha(kText, alpha), title);

			char sub[96];
			if (pass.culled)
				snprintf(sub, sizeof(sub), "culled");
			else
				snprintf(sub, sizeof(sub), "%s  R:%zu W:%zu", IsComputePass(pass) ? "Compute" : "Raster",
					pass.reads.size(), pass.writes.size());
			dl->AddText(font, fontSize, ImVec2(a.x + kNodePad * m_Zoom, a.y + headerH + kNodePad * 0.5f * m_Zoom),
				WithAlpha(kTextDim, alpha), sub);
		}
	}

	dl->PopClipRect();

	// ---- Tooltips -----------------------------------------------------------
	if (hoveredNode >= 0)
	{
		const RGPass& pass = passes[m_Nodes[hoveredNode].pass];
		ImGui::BeginTooltip();
		ImGui::Text("%s  (#%d)", pass.name.c_str(), m_Nodes[hoveredNode].execIndex);
		if (pass.culled)
			ImGui::TextDisabled("Culled / disabled");
		if (pass.barriers != BarrierFlags::None)
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kBarrierBadge), "Barrier: %s", BarrierString(pass.barriers).c_str());
		ImGui::SeparatorText("Shader reads / writes");
        DrawShaderAccesses(graph, pass);
        ImGui::SeparatorText("Draw attachments");
        DrawAttachments(graph, pass);
        ImGui::SeparatorText("Texture copies (source -> destination)");
        DrawCopies(graph, pass);
        ImGui::EndTooltip();
	}
	else if (hoveredEdge >= 0)
	{
		const EdgeGroup& g = m_Edges[hoveredEdge];
		ImGui::BeginTooltip();
		ImGui::Text("%s  ->  %s", PassName(graph, g.from).c_str(), PassName(graph, g.to).c_str());
		ImGui::Separator();
		std::set<std::pair<ResourceId, RGHazard>> shown;
		for (uint32_t idx : g.infos)
		{
			const RGEdgeInfo& info = infos[idx];
			if (!HazardVisible((int)info.hazard) || !shown.emplace(info.resource, info.hazard).second)
				continue;
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kHazardColor[(int)info.hazard]), "%s", HazardName(info.hazard));
			ImGui::SameLine();
			ImGui::TextUnformatted(ResourceName(graph, info.resource).c_str());
			DrawDependencyUses(graph, info);
		}
		ImGui::EndTooltip();
	}

	ImGui::EndChild();

	if (m_SelectedPass < passes.size())
		DrawPassDetails(graph);
}

void RenderGraphViewer::DrawPassDetails(const RenderGraph& graph)
{
	const auto& passes = graph.GetPasses();
	const RGPass& pass = passes[m_SelectedPass];
	const int node = m_SelectedPass < m_NodeOfPass.size() ? m_NodeOfPass[m_SelectedPass] : -1;

	ImGui::BeginChild("##RGDetails", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

	ImGui::Text("%s", pass.name.c_str());
	ImGui::SameLine();
	if (node >= 0)
		ImGui::TextDisabled("exec #%d  layer %d", m_Nodes[node].execIndex, m_Nodes[node].layer);
	ImGui::SameLine();
	ImGui::TextDisabled("| %s", pass.culled ? "culled" : (IsComputePass(pass) ? "Compute/Copy" : "Raster"));
	if (pass.barriers != BarrierFlags::None)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kBarrierBadge), "Barrier: %s", BarrierString(pass.barriers).c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Deselect"))
	{
		m_SelectedPass = UINT32_MAX;
		m_SelectedName.clear();
		ImGui::EndChild();
		return;
	}

	const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
	if (ImGui::BeginTable("##RGDetailCols", 4, flags))
	{
		ImGui::TableSetupColumn("Shader reads / writes", ImGuiTableColumnFlags_WidthFixed, 280.0f);
		ImGui::TableSetupColumn("Draw attachments", ImGuiTableColumnFlags_WidthFixed, 300.0f);
		ImGui::TableSetupColumn("Texture copies", ImGuiTableColumnFlags_WidthFixed, 320.0f);
		ImGui::TableSetupColumn("Dependencies", ImGuiTableColumnFlags_WidthFixed, 340.0f);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
        DrawShaderAccesses(graph, pass);
        ImGui::TableSetColumnIndex(1);
        DrawAttachments(graph, pass);
        ImGui::TableSetColumnIndex(2);
        DrawCopies(graph, pass);
        ImGui::TableSetColumnIndex(3);
        bool any = false;
		std::set<std::tuple<PassId, PassId, ResourceId, RGHazard>> shown;
		for (const RGEdgeInfo& e : graph.GetEdgeInfos())
		{
			const bool in = e.to == m_SelectedPass;
			const bool out = e.from == m_SelectedPass;
			if (!in && !out)
				continue;
			if (!shown.emplace(e.from, e.to, e.resource, e.hazard).second)
				continue;
			any = true;
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kHazardColor[(int)e.hazard]), "%s", HazardName(e.hazard));
			ImGui::SameLine();
			ImGui::Text("%s %s  via %s", in ? "<-" : "->", PassName(graph, in ? e.from : e.to).c_str(),
				ResourceName(graph, e.resource).c_str());
			DrawDependencyUses(graph, e);
		}
		if (!any) ImGui::TextDisabled("none");

		ImGui::EndTable();
	}

	ImGui::EndChild();
}

void RenderGraphViewer::DrawLifetimeTab(const RenderGraph& graph)
{
	const auto& passes = graph.GetPasses();
	const auto& resources = graph.GetResources();
	const auto& order = graph.GetExecutionOrder();
	const int passCount = (int)order.size();

	ImGui::SetNextItemWidth(140.0f);
	ImGui::Combo("Sort", &m_LifetimeSort, "First use\0Name\0");
	ImGui::SameLine();
	ImGui::Checkbox("Hide unused", &m_HideUnused);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.0f);
	ImGui::InputTextWithHint("##lsearch", "Search resource...", m_ResourceSearch, sizeof(m_ResourceSearch));
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Bar = first..last use in execution order\nFilled mark = write, hollow mark = read\n"
			"I = imported, E = exported\nThe selected pass's column is highlighted");

	// Per-resource access list in execution positions.
	struct Access { int exec; uint32_t pass; RGAccess access; bool write; };
	std::vector<std::vector<Access>> accesses(resources.size());
	for (int i = 0; i < passCount; ++i)
	{
		const PassId p = order[i];
		if (p >= passes.size())
			continue;
		for (const auto& r : passes[p].reads)
			if (r.resource < resources.size())
				accesses[r.resource].push_back({ i, p, r.access, false });
		for (const auto& w : passes[p].writes)
			if (w.resource < resources.size())
				accesses[w.resource].push_back({ i, p, w.access, true });
	}

	std::vector<uint32_t> rows;
	rows.reserve(resources.size());
	for (uint32_t i = 0; i < resources.size(); ++i)
	{
		if (m_HideUnused && resources[i].firstPass < 0)
			continue;
		if (m_ResourceSearch[0] != '\0' && !MatchesSearch(resources[i].name, m_ResourceSearch))
			continue;
		rows.push_back(i);
	}
	if (m_LifetimeSort == 0)
	{
		std::stable_sort(rows.begin(), rows.end(), [&](uint32_t a, uint32_t b)
		{
			const int fa = resources[a].firstPass < 0 ? INT32_MAX : resources[a].firstPass;
			const int fb = resources[b].firstPass < 0 ? INT32_MAX : resources[b].firstPass;
			if (fa != fb) return fa < fb;
			return resources[a].lastPass < resources[b].lastPass;
		});
	}
	else
	{
		std::stable_sort(rows.begin(), rows.end(), [&](uint32_t a, uint32_t b)
		{
			return resources[a].name < resources[b].name;
		});
	}

	int selectedExec = -1;
	for (int i = 0; i < passCount; ++i)
		if (order[i] == m_SelectedPass)
			selectedExec = i;

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
	if (!ImGui::BeginTable("##RGLifetimes", 4, flags))
		return;

	ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 220.0f);
	ImGui::TableSetupColumn("Desc", ImGuiTableColumnFlags_WidthFixed, 150.0f);
	ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 40.0f);
	ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableHeadersRow();

	const float rowH = ImGui::GetFrameHeight();
	ImDrawList* dl = ImGui::GetWindowDrawList();

	for (uint32_t resId : rows)
	{
		const RGResource& res = resources[resId];
		ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);

		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(res.name.c_str());

		ImGui::TableSetColumnIndex(1);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s %s", res.type == RGResourceType::Texture ? "Tex" : "Buf", ResourceDesc(res).c_str());

		ImGui::TableSetColumnIndex(2);
		ImGui::AlignTextToFramePadding();
		if (res.imported) { ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "I"); ImGui::SameLine(0.0f, 4.0f); }
		if (res.exported) { ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "E"); }
		if (!res.imported && !res.exported) ImGui::TextDisabled("-");

		ImGui::TableSetColumnIndex(3);
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float w = std::max(10.0f, ImGui::GetContentRegionAvail().x);
		ImGui::PushID((int)resId);
		ImGui::InvisibleButton("##tl", ImVec2(w, rowH));
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		if (passCount <= 0)
			continue;

		const float cell = w / (float)passCount;
		const float midY = p0.y + rowH * 0.5f;

		if (selectedExec >= 0)
			dl->AddRectFilled(ImVec2(p0.x + cell * selectedExec, p0.y), ImVec2(p0.x + cell * (selectedExec + 1), p0.y + rowH),
				IM_COL32(255, 220, 90, 40));

		if (res.firstPass >= 0)
		{
			const ImU32 barCol = res.type == RGResourceType::Texture ? IM_COL32(70, 120, 190, 200) : IM_COL32(150, 100, 190, 200);
			dl->AddRectFilled(ImVec2(p0.x + cell * res.firstPass + 1.0f, p0.y + rowH * 0.25f),
				ImVec2(p0.x + cell * (res.lastPass + 1) - 1.0f, p0.y + rowH * 0.75f), barCol, 3.0f);
		}

		const float mr = std::clamp(cell * 0.25f, 2.0f, rowH * 0.22f);
		for (const Access& acc : accesses[resId])
		{
			const ImVec2 c(p0.x + cell * (acc.exec + 0.5f), midY);
			if (acc.write)
				dl->AddCircleFilled(c, mr, IM_COL32(240, 240, 240, 255));
			else
				dl->AddCircle(c, mr, IM_COL32(240, 240, 240, 255), 0, 1.5f);
		}

		if (hovered)
		{
			const int col = std::clamp((int)((ImGui::GetIO().MousePos.x - p0.x) / cell), 0, passCount - 1);
			ImGui::BeginTooltip();
			ImGui::Text("%s", res.name.c_str());
			ImGui::TextDisabled("Lifetime: #%d .. #%d", res.firstPass, res.lastPass);
			ImGui::TextDisabled("Column #%d: %s", col, PassName(graph, order[col]).c_str());
			ImGui::Separator();
			for (const Access& acc : accesses[resId])
			{
				const bool atCol = acc.exec == col;
				if (atCol) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kNodeSelected));
				ImGui::Text("#%d %s  [%s] %s", acc.exec, PassName(graph, acc.pass).c_str(), AccessName(acc.access),
					acc.write ? "write" : "read");
				if (atCol) ImGui::PopStyleColor();
			}
			if (accesses[resId].empty())
				ImGui::TextDisabled("No pass accesses this resource");
			ImGui::EndTooltip();
		}
	}

	ImGui::EndTable();
}
