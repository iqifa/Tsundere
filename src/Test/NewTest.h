#ifndef NEWTEST
#define NEWTEST

#include"Scene/Mesh.h"
#include"Test/Test.h"

#include"Scene/Modle.h"

#include"Scene/Component.h"


using namespace Component;

namespace test
{
	class NewTest :public Test
	{
	public:
		NewTest();
		void OnRender()override;
	private:

		unique_ptr<Mesh> mesh;
		//unique_ptr<Shader> shader;
		Shader shader;
		unique_ptr<Texture> tex1;
		unique_ptr<Texture> tex2;

		unique_ptr<VertexArray> vao;
		unique_ptr<VertexBuffer>vbo;
		unique_ptr<IndexBuffer>ibo;

		unique_ptr<Model> model;
		vector<Texture_Struct>ts;

		vector<Mesh> meshes;
	};
}

#endif // !NEWTEST