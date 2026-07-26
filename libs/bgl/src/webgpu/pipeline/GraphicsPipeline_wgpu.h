#pragma once
#include "constants/constants.h"
#include "resource/Shader.h"
#include "types/Format.h"
#include "types/RenderState.h"
#include "uniforms/UniformLayoutEntry.h"

#include <core/containers/static_vector.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * The vertex + fragment stages, render state, and attachment formats a WebGPU graphics pipeline
	 * is built from. The two stages may come from different Slang modules -- the forward shaders keep
	 * the pixel stage separate from the geometry one -- and are linked into one WGSL program.
	 */
	struct GraphicsPipelineDesc
	{
		core::SharedRef<IShader>                        vertexShader = nullptr;
		core::SharedRef<IShader>                        pixelShader  = nullptr;
		std::string                                     vertexEntry  = "vs_main";
		std::string                                     pixelEntry   = "fs_main";
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;
		std::string                                     debugName;
	};

	/**
	 * A WebGPU graphics pipeline: the vertex and fragment entry points compiled to one WGSL module,
	 * reflected into a bind group layout (the same binding model the compute path uses), and built
	 * into a wgpu::RenderPipeline over the descriptor's colour and depth formats.
	 *
	 * This is a backend-internal building block, not an RHI IMeshletPipeline. The engine's raster
	 * seam stays IMeshletPipeline on both backends; because WebGPU has no mesh stage, its
	 * IMeshletPipeline is emulated by composing this GraphicsPipeline (the vertex-pulling draw) with
	 * a compute ComputePipeline (the meshlet-expansion kernel). That composition lands with the
	 * forward path.
	 */
	class GraphicsPipeline final
	{
	public:
		GraphicsPipeline(
			const wgpu::Device&         device,
			slang::ISession*            session,
			const GraphicsPipelineDesc& desc);

		GraphicsPipeline(const GraphicsPipeline&)     = delete;
		GraphicsPipeline(GraphicsPipeline&&) noexcept = delete;

		GraphicsPipeline&
		operator=(const GraphicsPipeline&) = delete;

		GraphicsPipeline&
		operator=(GraphicsPipeline&&) noexcept = delete;

		[[nodiscard]] UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept;

		[[nodiscard]] std::vector<std::string>
		GetUniformBufferNames() const noexcept;

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
