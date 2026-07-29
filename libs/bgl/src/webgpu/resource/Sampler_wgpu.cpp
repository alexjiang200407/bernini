#include "resource/Sampler_wgpu.h"

#include "convert_wgpu.h"

namespace bgl
{
	Sampler::Sampler(const wgpu::Device& device, const SamplerDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Sampler: null device");

		const auto samplerDesc = ToWgpuSamplerDescriptor(desc);
		m_Sampler              = device.CreateSampler(&samplerDesc);
	}
}
