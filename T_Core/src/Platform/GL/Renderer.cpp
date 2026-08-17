#include"Renderer.h"
#include"IndexBuffer.h"
#include"VertexBuffer.h"
#include"VertexArray.h"
#include"GLShader.h"
using namespace std;

void Renderer::DrawElement(const VertexArray& va, const IndexBuffer& ib, const RHIShader& shader)const
{
	va.Bind();
	ib.Bind();
	shader.Bind();
	GLCall(glDrawElements(GL_TRIANGLES, ib.GetCount(), GL_UNSIGNED_INT, nullptr));
}

void Renderer::DrawArray(const VertexArray& va, const RHIShader& shader) const
{
	va.Bind();
	shader.Bind();
	GLCall(glDrawArrays(GL_TRIANGLES, 0, va.GetCount()));
	//cout <<"Vertex Count"<< va.GetCount() << endl;
}

void Renderer::Clear()
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void Renderer::Clear_Color()
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::BeginScene()
{
}

void Renderer::EndScene()
{
}

void Renderer::Submission(Ref<VertexArray>& vertexArray,Ref<RHIShader>& shader)
{
	shader->Bind();
	vertexArray->Bind();
	GLCall(glDrawArrays(GL_TRIANGLES, 0, vertexArray->GetCount()));
}