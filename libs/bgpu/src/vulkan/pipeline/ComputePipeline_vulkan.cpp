#include "pipeline/ComputePipeline_vulkan.h"
#include "native_device_vulkan.h"
#include "pipeline/PipelineLayout_vulkan.h"
#include "shadercache/ShaderCache_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <bgpu/GpuContext.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/uniforms/UniformLayoutEntry.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	ComputePipeline::ComputePipeline(
		GpuContextRef              context,
		const ShaderCache*         cache,
		const ComputePipelineDesc& desc) : m_Context(std::move(context)), m_Desc(desc)
	{
		core::ensure(desc.shader != nullptr, "Compute shader cannot be null");

		const VkDevice device = GetVulkanHandles(*m_Context).device;
		m_Layout              = pipeline_util::BuildPipelineLayout(
			device,
			cache,
			{ desc.shader.Get() },
			VK_SHADER_STAGE_COMPUTE_BIT);

		const auto code = m_Layout.entryPointCode.find(desc.shader->GetDesc().entryPointName);
		core::ensure(
			code != m_Layout.entryPointCode.end(),
			"Missing SPIR-V for the compute shader");
		const std::vector<std::byte>& spirv = code->second;

		auto moduleInfo     = VkShaderModuleCreateInfo();
		moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleInfo.codeSize = spirv.size();
		moduleInfo.pCode    = reinterpret_cast<const uint32_t*>(spirv.data());

		VkShaderModule module = VK_NULL_HANDLE;
		EnsureVk(
			vkCreateShaderModule(device, &moduleInfo, nullptr, &module),
			"vkCreateShaderModule");

		// Slang names the one entry point of each module it emits `main`.
		auto stage   = VkPipelineShaderStageCreateInfo();
		stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = module;
		stage.pName  = "main";

		auto info   = VkComputePipelineCreateInfo();
		info.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage  = stage;
		info.layout = m_Layout.layout;

		const VkResult created = vkCreateComputePipelines(
			device,
			PipelineCacheOf(cache),
			1,
			&info,
			nullptr,
			&m_Pipeline);
		vkDestroyShaderModule(device, module, nullptr);
		EnsureVk(created, "vkCreateComputePipelines");

		SetVkDebugName(
			device,
			VK_OBJECT_TYPE_PIPELINE,
			reinterpret_cast<uint64_t>(m_Pipeline),
			desc.debugName);
	}

	ComputePipeline::~ComputePipeline() noexcept
	{
		spdlog::trace("~ComputePipeline");
		vkDestroyPipeline(GetVulkanHandles(*m_Context).device, m_Pipeline, nullptr);
	}

	UniformLayoutEntry
	ComputePipeline::GetUniformLayoutEntry(const std::string_view name) const noexcept
	{
		const auto found = m_Layout.uniformLayoutEntries.find(name);
		if (found == m_Layout.uniformLayoutEntries.end())
			core::fatal("Uniform layout entry not found: {}", name);
		return found->second;
	}

	std::vector<std::string>
	ComputePipeline::GetUniformBufferNames() const noexcept
	{
		auto names = std::vector<std::string>();
		names.reserve(m_Layout.uniformLayoutEntries.size());
		for (const auto& [name, entry] : m_Layout.uniformLayoutEntries) names.push_back(name);
		return names;
	}
}
