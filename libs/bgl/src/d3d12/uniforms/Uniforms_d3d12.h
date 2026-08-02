#pragma once
#include "uniforms/Uniforms.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The D3D12 constant-buffer mirror.
	 *
	 * A bindless handle reaches the shader as its index into the shader-visible descriptor heap,
	 * which the shader indexes directly. That index is the resource's pool slot for as long as the
	 * pools own their descriptors; once a DescriptorAllocator hands them out it is a lookup, and
	 * this is the only place that has to learn about it.
	 */
	class Uniforms final : public core::RefCounter<IUniforms>
	{
	public:
		Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName);
		Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName);

		Uniforms(const Uniforms&) noexcept = delete;
		Uniforms(Uniforms&&) noexcept      = delete;

		Uniforms&
		operator=(const Uniforms&) noexcept = delete;

		Uniforms&
		operator=(Uniforms&&) noexcept = delete;

		[[nodiscard]] DescriptorHandle
		ResolveBindless(const BufferHandle& handle) const noexcept override;

		[[nodiscard]] DescriptorHandle
		ResolveBindless(const SamplerHandle& handle) const noexcept override;

		[[nodiscard]] DescriptorHandle
		ResolveBindless(const TextureHandle& handle) const noexcept override;
	};
}
