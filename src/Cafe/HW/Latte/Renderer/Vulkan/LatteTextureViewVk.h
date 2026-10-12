#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRBase.h"

class LatteTextureViewVk : public LatteTextureView
{
public:
	LatteTextureViewVk(VkDevice device, class LatteTextureVk* texture, Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount);
	~LatteTextureViewVk();

	uint64 GetUniqueId() const { return m_uniqueId; };
	VKRObjectTextureView* GetViewRGBA();
	VKRObjectTextureView* GetSamplerView(uint32 gpuSamplerSwizzle);
	VkSampler GetDefaultTextureSampler(bool useLinearTexFilter);
	VkFormat GetFormat() const { return m_format; }

	LatteTextureVk* GetBaseImage() const { return (LatteTextureVk*)baseTexture; }

	void AddDescriptorSetReference(struct VkDescriptorSetInfo* dsInfo) { if (std::find(list_descriptorSets.begin(), list_descriptorSets.end(), dsInfo) == list_descriptorSets.end()) list_descriptorSets.emplace_back(dsInfo); };
	void RemoveDescriptorSetReference(struct VkDescriptorSetInfo* dsInfo) { list_descriptorSets.erase(std::remove(list_descriptorSets.begin(), list_descriptorSets.end(), dsInfo), list_descriptorSets.end()); };

private:
	VkImageViewType GetImageViewTypeFromGX2Dim(Latte::E_DIM dim);
	VKRObjectTextureView* CreateView(uint32 gpuSamplerSwizzle);

	inline static const uint32 CACHE_EMPTY_ENTRY = 0xFFFFFFFF;

	uint32 m_smallCacheSwizzle0 = { CACHE_EMPTY_ENTRY };
	uint32 m_smallCacheSwizzle1 = { CACHE_EMPTY_ENTRY };
	VKRObjectTextureView* m_smallCacheView0 = {};
	VKRObjectTextureView* m_smallCacheView1 = {};
	std::unordered_map<uint32, VKRObjectTextureView*>* m_fallbackCache{};

	VkDevice m_device;
	VkFormat m_format;
	std::vector<struct VkDescriptorSetInfo*> list_descriptorSets;

	uint64 m_uniqueId;
};
