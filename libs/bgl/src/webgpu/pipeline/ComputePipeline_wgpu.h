#pragma once
#include "pipeline/ComputePipeline.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/str/str.h>

namespace bgl
{
	/**
	 * A WebGPU compute pipeline: the kernel's entry point compiled to WGSL, reflected into a bind
	 * group layout, and built into a wgpu::ComputePipeline.
	 *
	 * WGSL has no bindless, so each plainly-bound buffer becomes an explicit (group, binding) slot.
	 * Reflection records that slot on every resource leaf of the ReflectedLayout, and assigns the
	 * leaf a synthetic byte offset in the CPU-side Uniforms buffer -- where the handle write lands
	 * and the CommandList reads it back at dispatch to bind the buffer.
	 */
	class ComputePipeline final : public core::RefCounter<IComputePipeline>
	{
	public:
		ComputePipeline(
			const wgpu::Device&        device,
			slang::ISession*           session,
			const ComputePipelineDesc& desc);

		~ComputePipeline() noexcept override = default;

		ComputePipeline(const ComputePipeline&)     = delete;
		ComputePipeline(ComputePipeline&&) noexcept = delete;

		ComputePipeline&
		operator=(const ComputePipeline&) = delete;

		ComputePipeline&
		operator=(ComputePipeline&&) noexcept = delete;

		const ComputePipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override;

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override;

		[[nodiscard]] const wgpu::ComputePipeline&
		GetPipeline() const noexcept
		{
			return m_Pipeline;
		}

		[[nodiscard]] const wgpu::BindGroupLayout&
		GetBindGroupLayout() const noexcept
		{
			return m_BindGroupLayout;
		}

	private:
		ComputePipelineDesc   m_Desc;
		wgpu::ComputePipeline m_Pipeline;
		wgpu::BindGroupLayout m_BindGroupLayout;
		UniformLayoutMap      m_UniformLayoutEntries;
	};
}
