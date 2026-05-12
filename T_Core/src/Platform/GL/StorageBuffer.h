#pragma once

#include "HeadLine.h"
#include "Core/Core.h"
#include"ExternalFiles.h"

class T_API StorageBuffer
{
public:
	StorageBuffer(size_t size, const void* data = nullptr, unsigned int binding = 0);
	~StorageBuffer();

	void BindToSlot(unsigned int binding) const;
	void SetData(const void* data, size_t size, size_t offset = 0) const;

	unsigned int GetID() const { return m_RendererID; }
	size_t GetSize() const { return m_Size; }

	static Ref<StorageBuffer> Create(size_t size, const void* data = nullptr, unsigned int binding = 0);

private:
	unsigned int m_RendererID = 0;
	size_t m_Size = 0;
};
