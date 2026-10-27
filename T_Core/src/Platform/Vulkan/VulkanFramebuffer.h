#pragma once

#include "Platform/RHI/RHIFramebuffer.h"
#include "Platform/Vulkan/VulkanTexture2D.h"
#include <vulkan/vulkan.h>

class T_API VulkanFramebuffer : public RHIFramebuffer
{
public:
    explicit VulkanFramebuffer(const FramebufferDesc& desc)
        : m_Width(desc.width), m_Height(desc.height), m_Samples(desc.samples)
    {
        for (const auto& attachment : desc.colorAttachments)
        {
            Texture2DDesc textureDesc;
            textureDesc.width = m_Width;
            textureDesc.height = m_Height;
            textureDesc.format = attachment.format;
            textureDesc.sampleCount = attachment.samples ? attachment.samples : m_Samples;
            textureDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
            m_Colors.push_back(RHITexture2D::Create(textureDesc));
        }

        if (desc.hasDepthStencil)
        {
            Texture2DDesc textureDesc;
            textureDesc.width = m_Width;
            textureDesc.height = m_Height;
            textureDesc.format = Format::D24_UNORM_S8_UINT;
            textureDesc.sampleCount = m_Samples;
            textureDesc.usage = TextureUsage::DepthStencil;
            m_Depth = RHITexture2D::Create(textureDesc);
        }
    }

    ~VulkanFramebuffer() override = default;

    void Bind() override {}
    void Unbind() override {}
    void Resize(uint32_t w, uint32_t h) override
    {
        m_Width = w;
        m_Height = h;
        for (auto& texture : m_Colors)
            texture->Resize(w, h);
        if (m_Depth)
            m_Depth->Resize(w, h);
    }

    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uintptr_t GetColorAttachmentID(uint32_t index = 0) const override
    {
        return index < m_Colors.size() ? m_Colors[index]->GetNativeID() : 0;
    }
    uintptr_t GetDepthAttachmentID() const override
    {
        return m_Depth ? m_Depth->GetNativeID() : 0;
    }
    uintptr_t GetFramebufferID() const override { return 0; }
    uint32_t GetColorAttachmentCount() const override
    {
        return static_cast<uint32_t>(m_Colors.size());
    }
    Format GetColorAttachmentFormat(uint32_t index = 0) const override
    {
        return index < m_Colors.size() ? m_Colors[index]->GetFormat() : Format::Unknown;
    }
    Format GetDepthAttachmentFormat() const override
    {
        return m_Depth ? m_Depth->GetFormat() : Format::Unknown;
    }
    uint32_t GetSampleCount() const override { return m_Samples; }
    RHITexture2D* GetColorAttachment(uint32_t index = 0) const override
    {
        return index < m_Colors.size() ? m_Colors[index].get() : nullptr;
    }
    RHITexture2D* GetDepthAttachment() const override
    {
        return m_Depth.get();
    }

private:
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_Samples = 1;
    std::vector<Ref<RHITexture2D>> m_Colors;
    Ref<RHITexture2D> m_Depth;
};

class T_API VulkanFramebufferView : public RHIFramebuffer
{
public:
    explicit VulkanFramebufferView(const FramebufferViewDesc& desc)
        : m_Width(desc.width), m_Height(desc.height), m_DepthFormat(desc.depthFormat)
    {
        m_Colors = desc.colorAttachments;
        m_Depth = desc.depthAttachment;
        if (!m_Colors.empty())
            m_Samples = m_Colors.front()->GetSampleCount();
        else if (m_Depth)
            m_Samples = m_Depth->GetSampleCount();
    }

    ~VulkanFramebufferView() override = default;
    void Bind() override {}
    void Unbind() override {}
    void Resize(uint32_t, uint32_t) override {}
    uint32_t GetWidth() const override { return m_Width; }
    uint32_t GetHeight() const override { return m_Height; }
    uintptr_t GetColorAttachmentID(uint32_t index = 0) const override
    {
        return index < m_Colors.size() && m_Colors[index]
            ? m_Colors[index]->GetNativeID() : 0;
    }
    uintptr_t GetDepthAttachmentID() const override
    {
        return m_Depth ? m_Depth->GetNativeID() : 0;
    }
    uintptr_t GetFramebufferID() const override { return 0; }
    uint32_t GetColorAttachmentCount() const override
    {
        return static_cast<uint32_t>(m_Colors.size());
    }
    Format GetColorAttachmentFormat(uint32_t index = 0) const override
    {
        return index < m_Colors.size() && m_Colors[index]
            ? m_Colors[index]->GetFormat() : Format::Unknown;
    }
    Format GetDepthAttachmentFormat() const override
    {
        return m_Depth ? m_Depth->GetFormat() : Format::Unknown;
    }
    uint32_t GetSampleCount() const override { return m_Samples; }
    RHITexture2D* GetColorAttachment(uint32_t index = 0) const override
    {
        return index < m_Colors.size() ? m_Colors[index] : nullptr;
    }
    RHITexture2D* GetDepthAttachment() const override { return m_Depth; }

private:
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_Samples = 1;
    Format m_DepthFormat = Format::Unknown;
    std::vector<RHITexture2D*> m_Colors;
    RHITexture2D* m_Depth = nullptr;
};
