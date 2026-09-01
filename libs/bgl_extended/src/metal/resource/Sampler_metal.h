#pragma once
#include "metal_cpp.h"

#include "convert_metal.h"

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
			sd->setLodBias(desc.mipBias);
			sd->setCompareFunction(ConvertReduction(desc.reductionType));

			// Without this the sampler has no gpuResourceID, and every bindless bind reads garbage.
			sd->setSupportArgumentBuffers(true);

			m_Sampler = NS::TransferPtr(device->newSamplerState(sd.get()));
			gassert(m_Sampler.get() != nullptr, "Metal sampler state creation failed");
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
		SamplerDesc                      m_Desc;
		NS::SharedPtr<MTL::SamplerState> m_Sampler;
	};
}
