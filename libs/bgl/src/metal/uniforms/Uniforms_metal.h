#pragma once
#include "uniforms/Uniforms.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The Metal constant-buffer mirror.
	 *
	 * Metal has no directly-indexed descriptor heap, so what the mirror carries is the resource's
	 * pool slot and the encoder rewrites it to a device address or an MTLResourceID at dispatch --
	 * see MapUniformHandlesToGpuAddresses. The slot is what that rewrite looks the resource up by,
	 * which is why it never turns into a descriptor index here.
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
