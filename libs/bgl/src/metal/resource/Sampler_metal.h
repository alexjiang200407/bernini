#pragma once
#include "metal_cpp.h"

#include "resource/Sampler.h"

namespace bgl
{
	// The Metal definition of the RHI's forward-declared `Sampler`. A MTL::SamplerState is the whole
	// object: unlike D3D12 there is no descriptor heap slot behind it, and a shader reaches it by the
	// MTLResourceID a bindless handle resolves to.
	//
	// Built with argument-buffer support, because that resource id only exists for a sampler that
	// declared it.
	class Sampler
	{
	public:
		Sampler() = default;

		Sampler(MTL::Device* device, const SamplerDesc& desc) : m_Desc(desc)
		{
			NS::SharedPtr<MTL::SamplerDescriptor> sd =
				NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

			sd->setMinFilter(
				desc.minFilter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
			sd->setMagFilter(
				desc.magFilter ? MTL::SamplerMinMagFilterLinear : MTL::SamplerMinMagFilterNearest);
			sd->setMipFilter(
				desc.mipFilter ? MTL::SamplerMipFilterLinear : MTL::SamplerMipFilterNearest);

			sd->setSAddressMode(ConvertAddressMode(desc.addressU));
			sd->setTAddressMode(ConvertAddressMode(desc.addressV));
			sd->setRAddressMode(ConvertAddressMode(desc.addressW));

			sd->setMaxAnisotropy(static_cast<NS::UInteger>(std::max(1.0f, desc.maxAnisotropy)));
			sd->setBorderColor(ConvertBorderColor(desc.borderColor));

			// Without this the sampler has no gpuResourceID, and every bindless bind reads garbage.
			sd->setSupportArgumentBuffers(true);

			m_Sampler = NS::TransferPtr(device->newSamplerState(sd.get()));
		}

		[[nodiscard]] MTL::SamplerState*
		GetMTLResource() const noexcept
		{
			return m_Sampler.get();
		}

		[[nodiscard]] const SamplerDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Sampler.get() == nullptr;
		}

	private:
		static MTL::SamplerAddressMode
		ConvertAddressMode(SamplerAddressMode mode) noexcept
		{
			switch (mode)
			{
			case SamplerAddressMode::kClamp:
				return MTL::SamplerAddressModeClampToEdge;
			case SamplerAddressMode::kWrap:
				return MTL::SamplerAddressModeRepeat;
			case SamplerAddressMode::kBorder:
				return MTL::SamplerAddressModeClampToBorderColor;
			case SamplerAddressMode::kMirror:
				return MTL::SamplerAddressModeMirrorRepeat;
			case SamplerAddressMode::kMirrorOnce:
				return MTL::SamplerAddressModeMirrorClampToEdge;
			}
			gfatal("Unhandled SamplerAddressMode");
		}

		// Metal offers three fixed border colours rather than an arbitrary one, so the RHI's Color is
		// matched to the nearest: transparent black, opaque black, or opaque white.
		static MTL::SamplerBorderColor
		ConvertBorderColor(const Color& color) noexcept
		{
			if (color.a < 0.5f)
				return MTL::SamplerBorderColorTransparentBlack;
			return (color.r + color.g + color.b) / 3.0f < 0.5f ?
			           MTL::SamplerBorderColorOpaqueBlack :
			           MTL::SamplerBorderColorOpaqueWhite;
		}

		SamplerDesc                      m_Desc;
		NS::SharedPtr<MTL::SamplerState> m_Sampler;
	};
}
