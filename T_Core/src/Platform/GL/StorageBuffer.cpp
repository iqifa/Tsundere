#include "StorageBuffer.h"

StorageBuffer::StorageBuffer(size_t size, const void* data, unsigned int binding)
	: m_Size(size)
{
	glGenBuffers(1, &m_RendererID);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
	glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
	if (binding > 0)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

StorageBuffer::~StorageBuffer()
{
	glDeleteBuffers(1, &m_RendererID);
}

void StorageBuffer::BindToSlot(unsigned int binding) const
{
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
}

void StorageBuffer::SetData(const void* data, size_t size, size_t offset) const
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

Ref<StorageBuffer> StorageBuffer::Create(size_t size, const void* data, unsigned int binding)
{
	return CreateRef<StorageBuffer>(size, data, binding);
}
