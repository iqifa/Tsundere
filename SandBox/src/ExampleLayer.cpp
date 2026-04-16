#include "ExampleLayer.h"
#include <Debug/Debug.h>
entt::entity m_SelectedContext = null;
void ShowDockSpace()
{
	static bool opt_fullscreen = true;
	static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;


	ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
	if (opt_fullscreen)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	}
	else
	{
		dockspace_flags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
	}

	if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
		window_flags |= ImGuiWindowFlags_NoBackground;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("DockSpace Demo", 0, window_flags);
	ImGui::PopStyleVar();
	if (opt_fullscreen)
		ImGui::PopStyleVar(2);

	// Submit the DockSpace
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
	}
	ImGui::End();

}
ExampleLayer::ExampleLayer(Ref<Scene>scene, std::string name) : BasePanel(name)
{
	this->m_Context = scene;
	m_SelectedContext = null;

	va = CreatePtr<VertexArray>(36);
	float position[] = {
			1.0f,1.0f,0.0f,
			0.0f,1.0f,0.0f,
			1.0f,0.0f,0.0f,
	};

	unsigned int indices[] = {
			1,2,3,
			1,2,3
	};
	vb = CreatePtr<VertexBuffer>(position, 4 * sizeof(float) * 4);
	ibo = CreatePtr<IndexBuffer>(indices, 6);
	VertexBufferLayout layout;
	layout.Push<float>(3);
	//layout.Push<float>(2);
	va->AddBuffer(*vb, layout);

	shader = CreatePtr<Shader>("D:\\Code\\C++\\Tsunder\\Graduate\\res\\shaders/Basic.shader");


	proj = ortho(0.0f, 1080.0f, 0.0f, 960.0f, -1.0f, 1.0f);
	view = translate(mat4(1.0f), vec3(-100, 0, 0));


	framebuffer = CreatePtr<FrameBuffer>();
}



void ExampleLayer::OnUpdate()
{
	Renderer renderer;
	framebuffer->Bind();
	renderer.Clear();
	{
		mat4 model = mat4(1.0f);
		mat4 mvp = proj * view * model;
		shader->Bind();
		//shader->SetUniformMat4f("u_MVP", mvp);
		renderer.DrawElement(*va, *ibo, *shader);
	}
	framebuffer->UnBind();
}

void ExampleLayer::OnImGuiRender()
{
	ShowDockSpace();
	ImGui::Begin(m_HeadTitle.c_str());

	if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
		m_SelectedContext = null;

	for (auto entityID : m_Context->m_Registry.view<Top>())
	{
		Entity entity = Entity{ m_Context.get(),entityID };

		DrawEntityNode(entity);
	}


	if (ImGui::BeginPopupContextWindow(0, 1))
	{
		if (ImGui::MenuItem("Create Empty Entity"))
		{
			//Entity entity = m_Context->CreateEntity("Empty Entity");
			//Entity childOne = m_Context->CreateEntity("ChildOne");

			//entity.addchildwithchangeparent(childOne);

			Entity entity = m_Context->CreateEntity("Empty Entity");
			if (m_SelectedContext != null)
			{
				Entity{ m_Context.get(),m_SelectedContext }.addchildwithchangeparent(entity);
			}
			
		}
		if (m_SelectedContext != null) {
			if (ImGui::MenuItem("Delete"))
			{
				for (auto entityID : m_Context->m_Registry.view<ID>())
				{
					Info_Core((int)entityID);
				}
				m_Context->DestoryEntity(Entity{ m_Context.get(),m_SelectedContext});
			}
		}

		ImGui::EndPopup();
	}
	ImGui::End();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0,0 });

	ImGui::Begin("ViewPort");
	ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
	if (m_ViewPortSize != *((vec2*)&viewportPanelSize))
	{
		m_ViewPortSize = { viewportPanelSize.x,viewportPanelSize.y };
		glViewport(0, 0, m_ViewPortSize.x, m_ViewPortSize.y);
		framebuffer->Rsetsize(m_ViewPortSize);
	}
	ImGui::Image((ImTextureID)(uintptr_t)framebuffer->GetClolorAttachmentRenderID(), ImVec2(m_ViewPortSize.x, m_ViewPortSize.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	ImGui::End();
	ImGui::PopStyleVar();
}

void ExampleLayer::OnEvent(Eventing::Event<>& event)
{
	event.Invoke();
}
#define Drop
#ifdef Drop
void ExampleLayer::DrawEntityNode(Entity entity)
{
	if (m_SelectedContext != null)
	{
		bool a = Entity{ m_Context.get(),m_SelectedContext }.HasComponent <Tag>();
	}


	auto& tag = entity.GetComponent<Tag>();
	ImGuiTreeNodeFlags flags = (m_SelectedContext == entity ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
	bool opened = ImGui::TreeNodeEx((void*)(entt::entity)entity, flags, tag.tag.c_str());

	if (ImGui::IsItemClicked())
	{
		m_SelectedContext = entity;
		Info_Core("Switch Select" + Entity{ m_Context.get(),m_SelectedContext }.GetComponent<Tag>().tag);
	}
	bool Deleted = false;
	/*if (ImGui::BeginPopupContextWindow(0, 1))
	{

		if (ImGui::MenuItem("Delete Empty"))
			Deleted = true;
		ImGui::EndPopup();
	}*/
	if (opened)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			DrawEntityNode(child);
		}


		ImGui::TreePop();
	}
	if (Deleted)
	{
		for (auto childID : entity.GetComponent<Child>().children)
		{
			Entity child = Entity{ m_Context.get(),childID };
			m_Context->DestoryEntity(child);
		}
		m_Context->DestoryEntity({ m_Context.get(),m_SelectedContext });
	}
}

#endif // Drop