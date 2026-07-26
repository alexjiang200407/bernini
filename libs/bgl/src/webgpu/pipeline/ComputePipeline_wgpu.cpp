#include "pipeline/ComputePipeline_wgpu.h"

#include "pipeline/PipelineReflect_wgpu.h"
#include "resource/Shader.h"
#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	ComputePipeline::ComputePipeline(
		const wgpu::Device&        device,
		slang::ISession*           session,
		const ComputePipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.shader != nullptr, "ComputePipeline: null shader");

		const std::string entryName = m_Desc.shader->GetDesc().entryPointName.empty() ?
		                                  "main" :
		                                  m_Desc.shader->GetDesc().entryPointName;

		const WgslEntryPoint entries[] = { { m_Desc.shader->GetSlangModule(), entryName } };

		auto                  owner  = Slang::ComPtr<slang::IComponentType>();
		slang::ProgramLayout* layout = LinkWgslProgram(session, entries, owner);

		SlangErrorChecker errChecker;
		auto              code = Slang::ComPtr<slang::IBlob>();
		owner->getEntryPointCode(0, 0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >>
			errChecker;
		const auto wgsl =
			std::string(static_cast<const char*>(code->getBufferPointer()), code->getBufferSize());

		auto slots = std::vector<BindGroupSlot>();
		ReflectWgslBindings(layout, m_UniformLayoutEntries, slots);

		m_BindGroupLayout = MakeWgslBindGroupLayout(device, slots, wgpu::ShaderStage::Compute);

		auto plDesc                  = wgpu::PipelineLayoutDescriptor{};
		plDesc.bindGroupLayoutCount  = 1;
		wgpu::BindGroupLayout bgls[] = { m_BindGroupLayout };
		plDesc.bindGroupLayouts      = bgls;
		auto pipelineLayout          = device.CreatePipelineLayout(&plDesc);

		auto wgslDesc      = wgpu::ShaderSourceWGSL{};
		wgslDesc.code      = std::string_view(wgsl);
		auto smDesc        = wgpu::ShaderModuleDescriptor{};
		smDesc.nextInChain = &wgslDesc;
		auto shaderModule  = device.CreateShaderModule(&smDesc);

		auto cpDesc               = wgpu::ComputePipelineDescriptor{};
		cpDesc.label              = std::string_view(m_Desc.debugName);
		cpDesc.layout             = pipelineLayout;
		cpDesc.compute.module     = shaderModule;
		cpDesc.compute.entryPoint = std::string_view(entryName);

		m_Pipeline = device.CreateComputePipeline(&cpDesc);
	}

	UniformLayoutEntry
	ComputePipeline::GetUniformLayoutEntry(std::string_view name) const noexcept
	{
		auto it = m_UniformLayoutEntries.find(name);
		if (it != m_UniformLayoutEntries.end())
			return it->second;

		gfatal("Uniform layout entry not found: {}", name);
	}

	std::vector<std::string>
	ComputePipeline::GetUniformBufferNames() const noexcept
	{
		auto names = std::vector<std::string>();
		names.reserve(m_UniformLayoutEntries.size());
		for (const auto& [name, entry] : m_UniformLayoutEntries) names.push_back(name);
		return names;
	}
}
