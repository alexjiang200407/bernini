#pragma once
#include "constants/constants.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/Shader.h"
#include "types/Format.h"
#include "types/RenderState.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/containers/static_vector.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * A WebGPU graphics pipeline: the vertex and fragment entry points compiled to one WGSL module,
	 * reflected into a bind group layout (the same binding model the compute path uses), and built
	 * into a wgpu::RenderPipeline over the descriptor's colour and depth formats.
	 *
	 * Serves two roles: the RHI's IGraphicsPipeline for geometry drawn from a vertex count, and the
	 * raster half of the emulated IMeshletPipeline, which composes it with the meshlet-expansion
	 * compute kernel because WebGPU has no mesh stage.
	 */
	class GraphicsPipeline final : public core::RefCounter<IGraphicsPipeline>
	{
	public:
		GraphicsPipeline(
			const wgpu::Device&         device,
			slang::ISession*            session,
			const GraphicsPipelineDesc& desc);

		~GraphicsPipeline() noexcept override = default;

		GraphicsPipeline(const GraphicsPipeline&)     = delete;
		GraphicsPipeline(GraphicsPipeline&&) noexcept = delete;

		GraphicsPipeline&
		operator=(const GraphicsPipeline&) = delete;

		GraphicsPipeline&
		operator=(GraphicsPipeline&&) noexcept = delete;

		const GraphicsPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override;

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override;

		[[nodiscard]] const wgpu::RenderPipeline&
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
		GraphicsPipelineDesc  m_Desc;
		wgpu::RenderPipeline  m_Pipeline;
		wgpu::BindGroupLayout m_BindGroupLayout;
		UniformLayoutMap      m_UniformLayoutEntries;
	};
}
