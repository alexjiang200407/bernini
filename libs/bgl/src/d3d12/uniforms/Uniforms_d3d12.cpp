#include "uniforms/Uniforms_d3d12.h"

namespace bgl
{
	Uniforms::Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName) :
		core::RefCounter<IUniforms>(pipeline, cbufferName)
	{}

	Uniforms::Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName) :
		core::RefCounter<IUniforms>(pipeline, cbufferName)
	{}

	DescriptorHandle
	Uniforms::ResolveBindless(const BufferHandle& handle) const noexcept
	{
		return DescriptorHandle(handle.slot);
	}

	DescriptorHandle
	Uniforms::ResolveBindless(const SamplerHandle& handle) const noexcept
	{
		return DescriptorHandle(handle.idx);
	}

	DescriptorHandle
	Uniforms::ResolveBindless(const TextureHandle& handle) const noexcept
	{
		return DescriptorHandle(handle.slot);
	}
}
