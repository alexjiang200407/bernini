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
	 * The vertex + fragment stages, render state, and attachment formats a WebGPU render pipeline is
	 * built from. Both entry points live in one Slang module -- the shared forward source declares
	 * them side by side -- so the program links exactly the way a lone compute entry does.
	 */
	struct RenderPipelineDesc
	{
		core::SharedRef<IShader>                        shader      = nullptr;
		std::string                                     vertexEntry = "vs_main";
		std::string                                     pixelEntry  = "fs_main";
		RenderState                                     renderState;
		core::static_vector<Format, c_MaxRenderTargets> rtvFormats;
		Format                                          dsvFormat = Format::UNKNOWN;
		std::string                                     debugName;
	};

	/**
	 * A WebGPU render pipeline: the vertex and fragment entry points compiled to one WGSL module,
	 * reflected into a bind group layout (the same binding model the compute path uses), and built
	 * into a wgpu::RenderPipeline over the descriptor's colour and depth formats.
	 *
	 * This is a backend-internal object, not yet an RHI IMeshletPipeline: the engine's raster path is
	 * mesh-shader based, and mapping that onto WebGPU needs the meshlet-expansion kernel that lands
	 * with the vertex-pulling forward path. This is the render-pipeline half that path draws with.
	 */
	class RenderPipeline final
	{
	public:
		RenderPipeline(
			const wgpu::Device&       device,
			slang::ISession*          session,
			const RenderPipelineDesc& desc);

		RenderPipeline(const RenderPipeline&)     = delete;
		RenderPipeline(RenderPipeline&&) noexcept = delete;

		RenderPipeline&
		operator=(const RenderPipeline&) = delete;

		RenderPipeline&
		operator=(RenderPipeline&&) noexcept = delete;

		[[nodiscard]] UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept;

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
		RenderPipelineDesc    m_Desc;
		wgpu::RenderPipeline  m_Pipeline;
		wgpu::BindGroupLayout m_BindGroupLayout;
		UniformLayoutMap      m_UniformLayoutEntries;
	};
}
