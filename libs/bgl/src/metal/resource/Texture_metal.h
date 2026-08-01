#pragma once
#include "metal_cpp.h"

#include "constants/constants.h"
#include "resource/Texture.h"
#include "util_metal.h"

namespace bgl
{
	// The Metal definition of the RHI's forward-declared `Texture`: a private MTL::Texture carrying
	// the render-target / shader-read usage its TextureUsage implies.
	class Texture
	{
	public:
		Texture() = default;

		Texture(MTL::Device* device, const TextureDesc& desc) : m_Desc(desc)
		{
			gassert(desc.width > 0 && desc.height > 0, "Texture requires non-zero dimensions");
			gassert(desc.format != Format::UNKNOWN, "Texture requires a valid format");

			NS::SharedPtr<MTL::TextureDescriptor> td =
				NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
			td->setTextureType(ConvertTextureType(desc.dimension));
			td->setPixelFormat(ConvertFormat(desc.format));
			td->setWidth(desc.width);
			td->setHeight(desc.height);
			if (desc.mipLevels > 1)
				td->setMipmapLevelCount(desc.mipLevels);
			if (desc.sampleCount > 1)
				td->setSampleCount(desc.sampleCount);
			td->setStorageMode(MTL::StorageModePrivate);

			// arraySize counts slices, faces included, which is what D3D12's DepthOrArraySize means.
			// Metal's arrayLength counts whole textures, so a cube's six faces are not part of it.
			switch (desc.dimension)
			{
			case TextureDimension::kTexture3D:
				td->setDepth(desc.depth);
				break;
			case TextureDimension::kTextureCube:
				gassert(desc.arraySize == c_CubeFaceCount, "A cube map has exactly six faces");
				break;
			case TextureDimension::kTextureCubeArray:
				gassert(
					desc.arraySize % c_CubeFaceCount == 0,
					"A cube-map array's slice count must be a multiple of six");
				td->setArrayLength(desc.arraySize / c_CubeFaceCount);
				break;
			default:
				if (desc.arraySize > 1)
					td->setArrayLength(desc.arraySize);
				break;
			}

			MTL::TextureUsage usage = MTL::TextureUsageUnknown;
			if (desc.usage.any(TextureUsageFlag::kSRV))
				usage |= MTL::TextureUsageShaderRead;
			if (desc.usage.any(TextureUsageFlag::kRenderTarget) ||
			    desc.usage.any(TextureUsageFlag::kDepthStencil))
				usage |= MTL::TextureUsageRenderTarget;
			td->setUsage(usage);

			m_Texture = NS::TransferPtr(device->newTexture(td.get()));
			gassert(m_Texture.get() != nullptr, "Metal texture allocation failed");
			if (!desc.debugName.empty())
				m_Texture->setLabel(
					NS::String::string(desc.debugName.c_str(), NS::UTF8StringEncoding));
		}

		[[nodiscard]] MTL::Texture*
		GetMTLResource() const noexcept
		{
			return m_Texture.get();
		}

		[[nodiscard]] const TextureDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Texture.get() == nullptr;
		}

	private:
		TextureDesc                 m_Desc;
		NS::SharedPtr<MTL::Texture> m_Texture;
	};
}
