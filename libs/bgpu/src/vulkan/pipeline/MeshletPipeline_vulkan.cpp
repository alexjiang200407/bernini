#include "pipeline/MeshletPipeline_vulkan.h"
#include "convert_vulkan.h"
#include "native_device_vulkan.h"
#include "pipeline/PipelineLayout_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/Format.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <cstddef>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	MeshletPipeline::MeshletPipeline(
		GpuContextRef              context,
		const ShaderCache*         cache,
		const MeshletPipelineDesc& desc) : m_Context(std::move(context)), m_Desc(desc)
	{
		core::ensure(desc.meshShader != nullptr, "Mesh shader cannot be null");

		const VkDevice           device = GetVulkanHandles(*m_Context).device;
		const VkShaderStageFlags stages = VK_SHADER_STAGE_TASK_BIT_EXT |
		                                  VK_SHADER_STAGE_MESH_BIT_EXT |
		                                  VK_SHADER_STAGE_FRAGMENT_BIT;
		m_Layout                        = pipeline_util::BuildPipelineLayout(
			device,
			cache,
			{ desc.meshShader.Get(), desc.pixelShader.Get(), desc.ampShader.Get() },
			stages);

		struct Stage
		{
			const core::SharedRef<IShader>* shader;
			VkShaderStageFlagBits           bit;
		};
		const auto candidates = std::to_array<Stage>({
			{ &desc.ampShader, VK_SHADER_STAGE_TASK_BIT_EXT },
			{ &desc.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT },
			{ &desc.pixelShader, VK_SHADER_STAGE_FRAGMENT_BIT },
		});

		auto modules    = core::static_vector<VkShaderModule, 3>();
		auto stageInfos = core::static_vector<VkPipelineShaderStageCreateInfo, 3>();
		for (const auto& [shader, bit] : candidates)
		{
			if (*shader == nullptr)
				continue;

			const auto code = m_Layout.entryPointCode.find((*shader)->GetDesc().entryPointName);
			core::ensure(
				code != m_Layout.entryPointCode.end(),
				"Missing SPIR-V for a meshlet stage");
			const std::vector<std::byte>& spirv = code->second;

			auto moduleInfo     = VkShaderModuleCreateInfo();
			moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = spirv.size();
			moduleInfo.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());

			VkShaderModule module = VK_NULL_HANDLE;
			EnsureVk(
				vkCreateShaderModule(device, &moduleInfo, nullptr, &module),
				"vkCreateShaderModule");
			modules.push_back(module);

			// Slang names the one entry point of each module it emits `main`.
			auto stage   = VkPipelineShaderStageCreateInfo();
			stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stage.stage  = bit;
			stage.module = module;
			stage.pName  = "main";
			stageInfos.push_back(stage);
		}

		auto colorFormats = core::static_vector<VkFormat, c_MaxRenderTargets>();
		auto blends =
			core::static_vector<VkPipelineColorBlendAttachmentState, c_MaxRenderTargets>();
		for (size_t i = 0; i < desc.rtvFormats.size(); ++i)
		{
			colorFormats.push_back(ConvertFormat(desc.rtvFormats[i]));
			blends.push_back(ConvertBlendTarget(desc.renderState.blendState.targets[i]));
		}

		const VkFormat           depthFormat = ConvertFormat(desc.dsvFormat);
		const VkImageAspectFlags depthAspects =
			desc.dsvFormat == Format::UNKNOWN ? 0 : FormatAspects(desc.dsvFormat);

		auto rendering                    = VkPipelineRenderingCreateInfo();
		rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		rendering.colorAttachmentCount    = static_cast<uint32_t>(colorFormats.size());
		rendering.pColorAttachmentFormats = colorFormats.data();
		if ((depthAspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
			rendering.depthAttachmentFormat = depthFormat;
		if ((depthAspects & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
			rendering.stencilAttachmentFormat = depthFormat;

		// Viewports and scissors, any number of each, are the draw's (SetMeshletState).
		auto viewport  = VkPipelineViewportStateCreateInfo();
		viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

		const auto dynamicStates = std::to_array(
			{ VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT });
		auto dynamic              = VkPipelineDynamicStateCreateInfo();
		dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
		dynamic.pDynamicStates    = dynamicStates.data();

		const auto raster       = ConvertRasterState(desc.renderState.rasterState);
		const auto depthStencil = ConvertDepthStencilState(desc.renderState.depthStencilState);

		// D3D12's meshlet pipelines are single-sampled.
		auto multisample                 = VkPipelineMultisampleStateCreateInfo();
		multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisample.alphaToCoverageEnable =
			desc.renderState.blendState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

		auto blend            = VkPipelineColorBlendStateCreateInfo();
		blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.attachmentCount = static_cast<uint32_t>(blends.size());
		blend.pAttachments    = blends.data();

		auto info                = VkGraphicsPipelineCreateInfo();
		info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		info.pNext               = &rendering;
		info.stageCount          = static_cast<uint32_t>(stageInfos.size());
		info.pStages             = stageInfos.data();
		info.pViewportState      = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState   = &multisample;
		info.pDepthStencilState  = &depthStencil;
		info.pColorBlendState    = &blend;
		info.pDynamicState       = &dynamic;
		info.layout              = m_Layout.layout;

		const VkResult created =
			vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_Pipeline);
		for (const VkShaderModule module : modules) vkDestroyShaderModule(device, module, nullptr);
		EnsureVk(created, "vkCreateGraphicsPipelines");

		SetVkDebugName(
			device,
			VK_OBJECT_TYPE_PIPELINE,
			reinterpret_cast<uint64_t>(m_Pipeline),
			desc.meshShader->GetDesc().entryPointName);
	}

	MeshletPipeline::~MeshletPipeline() noexcept
	{
		spdlog::trace("~MeshletPipeline");
		vkDestroyPipeline(GetVulkanHandles(*m_Context).device, m_Pipeline, nullptr);
	}

	UniformLayoutEntry
	MeshletPipeline::GetUniformLayoutEntry(const std::string_view name) const noexcept
	{
		const auto found = m_Layout.uniformLayoutEntries.find(name);
		if (found == m_Layout.uniformLayoutEntries.end())
			core::fatal("Uniform layout entry not found: {}", name);
		return found->second;
	}

	std::vector<std::string>
	MeshletPipeline::GetUniformBufferNames() const noexcept
	{
		auto names = std::vector<std::string>();
		names.reserve(m_Layout.uniformLayoutEntries.size());
		for (const auto& [name, entry] : m_Layout.uniformLayoutEntries) names.push_back(name);
		return names;
	}
}
