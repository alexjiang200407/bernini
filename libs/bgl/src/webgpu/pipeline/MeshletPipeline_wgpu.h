#pragma once
#include "pipeline/GraphicsPipeline_wgpu.h"
#include "pipeline/MeshletPipeline.h"

namespace bgl
{
	/**
	 * The WebGPU IMeshletPipeline. WebGPU has no mesh stage, so the mesh path is emulated: the mesh
	 * module's BGL_WGSL arm carries a vertex-pulling VSMain (and, later, a meshlet-expansion compute
	 * entry), which this composes with the pixel shader into a GraphicsPipeline. DispatchMesh becomes
	 * dispatch-expand then drawIndirect once the expansion kernel is wired.
	 *
	 * The uniform reflection the kernel/binding machinery reads is the GraphicsPipeline's, so
	 * CreateMeshletKernel and Uniforms work unchanged. The vertex and pixel entries may come from
	 * different modules -- the forward shaders keep the pixel stage separate from the geometry one.
	 */
	class MeshletPipeline final : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			const wgpu::Device&        device,
			slang::ISession*           session,
			const MeshletPipelineDesc& desc);

		~MeshletPipeline() noexcept override = default;

		MeshletPipeline(const MeshletPipeline&)     = delete;
		MeshletPipeline(MeshletPipeline&&) noexcept = delete;

		MeshletPipeline&
		operator=(const MeshletPipeline&) = delete;

		MeshletPipeline&
		operator=(MeshletPipeline&&) noexcept = delete;

		const MeshletPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override
		{
			return m_Graphics->GetUniformLayoutEntry(name);
		}

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override
		{
			return m_Graphics->GetUniformBufferNames();
		}

		[[nodiscard]] const GraphicsPipeline&
		GetGraphicsPipeline() const noexcept
		{
			return *m_Graphics;
		}

	private:
		MeshletPipelineDesc               m_Desc;
		core::SharedRef<GraphicsPipeline> m_Graphics;
	};
}
