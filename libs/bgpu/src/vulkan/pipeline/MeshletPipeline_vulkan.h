#pragma once
#include "pipeline/PipelineLayout_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bgpu
{
	class ShaderCache;

	/**
	 * A graphics pipeline of a task, mesh and pixel stage, built from the SPIR-V the context's
	 * sessions emit for dynamic rendering into the formats its desc names. Its constant buffers are
	 * bound in set 0 and the bindless table in set 1, as a compute pipeline's are; its viewports and
	 * scissors are set per draw. Holds the context, so the device outlives it.
	 */
	class MeshletPipeline final : public core::RefCounter<IMeshletPipeline>
	{
	public:
		MeshletPipeline(
			GpuContextRef              context,
			const ShaderCache*         cache,
			const MeshletPipelineDesc& desc);
		~MeshletPipeline() noexcept override;

		MeshletPipeline(const MeshletPipeline&) = delete;
		MeshletPipeline(MeshletPipeline&&)      = delete;
		MeshletPipeline&
		operator=(const MeshletPipeline&) = delete;
		MeshletPipeline&
		operator=(MeshletPipeline&&) = delete;

		[[nodiscard]] const MeshletPipelineDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		[[nodiscard]] UniformLayoutEntry
		GetUniformLayoutEntry(std::string_view name) const noexcept override;

		[[nodiscard]] std::vector<std::string>
		GetUniformBufferNames() const noexcept override;

		[[nodiscard]] VkPipeline
		GetVkPipeline() const noexcept
		{
			return m_Pipeline;
		}

		[[nodiscard]] VkPipelineLayout
		GetVkPipelineLayout() const noexcept
		{
			return m_Layout.layout;
		}

		[[nodiscard]] VkDescriptorSetLayout
		GetConstantsSetLayout() const noexcept
		{
			return m_Layout.constantsLayout;
		}

		/** The binding in set 0 of each constant buffer, by its root parameter index. */
		[[nodiscard]] std::span<const uint32_t>
		GetConstantBufferBindings() const noexcept
		{
			return m_Layout.cbufferBindings;
		}

	private:
		// Declared first, destroyed last: the layout and the pipeline belong to its device.
		GpuContextRef                 m_Context;
		MeshletPipelineDesc           m_Desc;
		pipeline_util::PipelineLayout m_Layout;
		VkPipeline                    m_Pipeline = VK_NULL_HANDLE;
	};
}
