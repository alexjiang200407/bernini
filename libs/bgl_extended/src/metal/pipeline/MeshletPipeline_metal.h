#pragma once
#include "metal_cpp.h"
#include "types/ShaderStage.h"
#include <Foundation/NSSharedPtr.hpp>
#include <Metal/MTLDepthStencil.hpp>
#include <Metal/MTLDevice.hpp>
#include <Metal/MTLRenderPipeline.hpp>
#include <Metal/MTLTypes.hpp>
#include <bgl_common/gassert.h>

#include "pipeline/MeshletPipeline.h"
#include "pipeline/MetalPipelineReflection.h"
#include "uniforms/UniformLayoutEntry.h"

#include <array>
#include <core/ref/RefCounter.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bgl
{
	class ShaderCache;

	/**
	 * The Metal meshlet (mesh-shader) render pipeline. Each stage is compiled to its own MSL
	 * library (a whole-program compile drops mesh interpolant attributes), then combined into an
	 * MTL::RenderPipelineState via a MeshRenderPipelineDescriptor. The object/mesh threadgroup
	 * sizes come from the shaders' [numthreads]; the render targets are described per dispatch, not
	 * baked into the PSO.
	 */
	class MeshletPipeline final : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			MTL::Device*               device,
			ShaderCache*               shaderCache,
			const MeshletPipelineDesc& desc);

		const MeshletPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override
		{
			auto it = m_UniformLayoutEntries.find(name);
			gassert(it != m_UniformLayoutEntries.end(), "Unknown uniform buffer name");
			return it->second;
		}

		std::vector<std::string>
		GetUniformBufferNames() const noexcept override
		{
			std::vector<std::string> names;
			names.reserve(m_UniformLayoutEntries.size());
			for (const auto& [name, entry] : m_UniformLayoutEntries) names.push_back(name);
			return names;
		}

		[[nodiscard]] MTL::RenderPipelineState*
		GetMTLPipelineState() const noexcept
		{
			return m_PipelineState.get();
		}

		// Null on a mesh-only pipeline, which never draws.
		[[nodiscard]] MTL::DepthStencilState*
		GetMTLDepthStencilState() const noexcept
		{
			return m_DepthStencilState.get();
		}

		[[nodiscard]] MTL::Size
		GetThreadsPerObjectThreadgroup() const noexcept
		{
			return m_ThreadsPerObject;
		}

		[[nodiscard]] MTL::Size
		GetThreadsPerMeshThreadgroup() const noexcept
		{
			return m_ThreadsPerMesh;
		}

		[[nodiscard]] const std::vector<HandleSlot>&
		GetHandleOffsets(std::string_view name) const noexcept
		{
			auto it = m_HandleOffsets.find(name);
			gassert(it != m_HandleOffsets.end(), "Unknown uniform buffer name");
			return it->second;
		}

		/**
		 * The [[buffer(N)]] index `name` has in one stage's compiled MSL, or nullptr when that
		 * stage's program does not declare it. Indices are per-stage (each stage compiles as its
		 * own program), so a cbuffer must be bound at each stage's own index -- binding one index
		 * to every stage lets two cbuffers collide on a slot.
		 */
		[[nodiscard]] const uint32_t*
		GetStageBinding(ShaderStage stage, std::string_view name) const noexcept
		{
			const MetalStageBindingMap& bindings = m_StageBindings[static_cast<size_t>(stage)];

			auto it = bindings.find(name);
			return it != bindings.end() ? &it->second : nullptr;
		}

	private:
		[[nodiscard]] NS::SharedPtr<MTL::DepthStencilState>
		BuildDepthStencilState(MTL::Device* device) const;

		MeshletPipelineDesc                                  m_Desc;
		NS::SharedPtr<MTL::RenderPipelineState>              m_PipelineState;
		NS::SharedPtr<MTL::DepthStencilState>                m_DepthStencilState;
		UniformLayoutMap                                     m_UniformLayoutEntries;
		MetalHandleOffsetMap                                 m_HandleOffsets;
		std::array<MetalStageBindingMap, c_ShaderStageCount> m_StageBindings;
		MTL::Size m_ThreadsPerObject = MTL::Size(1, 1, 1);
		MTL::Size m_ThreadsPerMesh   = MTL::Size(1, 1, 1);
	};
}
