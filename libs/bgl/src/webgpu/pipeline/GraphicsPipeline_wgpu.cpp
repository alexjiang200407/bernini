#include "pipeline/GraphicsPipeline_wgpu.h"

#include "convert_wgpu.h"
#include "pipeline/PipelineReflect_wgpu.h"
#include "slang/SlangErrorChecker.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	namespace
	{
		wgpu::DepthStencilState
		MakeDepthStencilState(const DepthStencilState& state, Format format)
		{
			auto result   = wgpu::DepthStencilState{};
			result.format = ToWgpuTextureFormat(format);
			result.depthWriteEnabled =
				state.depthWriteEnable ? wgpu::OptionalBool::True : wgpu::OptionalBool::False;
			result.depthCompare = state.depthTestEnable ? ToWgpuCompareFunction(state.depthFunc) :
			                                              wgpu::CompareFunction::Always;

			const auto stencilFace = [](const DepthStencilState::StencilOpDesc& face) {
				auto out        = wgpu::StencilFaceState{};
				out.compare     = ToWgpuCompareFunction(face.stencilFunc);
				out.failOp      = ToWgpuStencilOperation(face.failOp);
				out.depthFailOp = ToWgpuStencilOperation(face.depthFailOp);
				out.passOp      = ToWgpuStencilOperation(face.passOp);
				return out;
			};

			result.stencilFront = stencilFace(state.frontFaceStencil);
			result.stencilBack  = stencilFace(state.backFaceStencil);
			result.stencilReadMask =
				state.stencilEnable ? static_cast<uint32_t>(state.stencilReadMask) : 0u;
			result.stencilWriteMask =
				state.stencilEnable ? static_cast<uint32_t>(state.stencilWriteMask) : 0u;

			return result;
		}
	}

	GraphicsPipeline::GraphicsPipeline(
		const wgpu::Device&         device,
		slang::ISession*            session,
		const GraphicsPipelineDesc& desc) : m_Desc(desc)
	{
		gassert(m_Desc.shader != nullptr, "GraphicsPipeline: null shader");
		gassert(!m_Desc.rtvFormats.empty(), "GraphicsPipeline: needs at least one colour target");

		const std::string entries[] = { m_Desc.vertexEntry, m_Desc.pixelEntry };

		auto                  owner  = Slang::ComPtr<slang::IComponentType>();
		slang::ProgramLayout* layout = LinkWgslProgram(
			session,
			m_Desc.shader->GetSlangModule(),
			std::span<const std::string>(entries, std::size(entries)),
			owner);

		SlangErrorChecker errChecker;
		auto              code = Slang::ComPtr<slang::IBlob>();
		owner->getTargetCode(0, code.writeRef(), errChecker.WriteDiagnosticBlob()) >> errChecker;
		const auto wgsl =
			std::string(static_cast<const char*>(code->getBufferPointer()), code->getBufferSize());

		auto slots = std::vector<BindGroupSlot>();
		ReflectWgslBindings(layout, m_UniformLayoutEntries, slots);

		m_BindGroupLayout = MakeWgslBindGroupLayout(
			device,
			slots,
			wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);

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

		const RasterState& raster = m_Desc.renderState.rasterState;

		// One BlendState per target, kept alive in a vector so the ColorTargetState pointers into it
		// stay valid until CreateRenderPipeline reads them.
		auto blends  = std::vector<wgpu::BlendState>();
		auto targets = std::vector<wgpu::ColorTargetState>();
		blends.reserve(m_Desc.rtvFormats.size());
		targets.reserve(m_Desc.rtvFormats.size());

		for (uint32_t i = 0; i < m_Desc.rtvFormats.size(); ++i)
		{
			const BlendState::RenderTarget& rt = m_Desc.renderState.blendState.targets[i];

			auto target      = wgpu::ColorTargetState{};
			target.format    = ToWgpuTextureFormat(m_Desc.rtvFormats[i]);
			target.writeMask = ToWgpuColorWriteMask(rt.colorWriteMask);

			if (rt.blendEnable)
			{
				auto& blend           = blends.emplace_back();
				blend.color.srcFactor = ToWgpuBlendFactor(rt.srcBlend);
				blend.color.dstFactor = ToWgpuBlendFactor(rt.destBlend);
				blend.color.operation = ToWgpuBlendOperation(rt.blendOp);
				blend.alpha.srcFactor = ToWgpuBlendFactor(rt.srcBlendAlpha);
				blend.alpha.dstFactor = ToWgpuBlendFactor(rt.destBlendAlpha);
				blend.alpha.operation = ToWgpuBlendOperation(rt.blendOpAlpha);
				target.blend          = &blend;
			}

			targets.push_back(target);
		}

		auto fragment        = wgpu::FragmentState{};
		fragment.module      = shaderModule;
		fragment.entryPoint  = std::string_view(m_Desc.pixelEntry);
		fragment.targetCount = targets.size();
		fragment.targets     = targets.data();

		auto rpDesc                = wgpu::RenderPipelineDescriptor{};
		rpDesc.label               = std::string_view(m_Desc.debugName);
		rpDesc.layout              = pipelineLayout;
		rpDesc.vertex.module       = shaderModule;
		rpDesc.vertex.entryPoint   = std::string_view(m_Desc.vertexEntry);
		rpDesc.primitive.topology  = wgpu::PrimitiveTopology::TriangleList;
		rpDesc.primitive.cullMode  = ToWgpuCullMode(raster.cullMode);
		rpDesc.primitive.frontFace = ToWgpuFrontFace(raster.frontCounterClockwise);
		rpDesc.multisample.count   = 1;
		rpDesc.multisample.mask    = 0xFFFFFFFF;
		rpDesc.fragment            = &fragment;

		auto depthStencil = wgpu::DepthStencilState{};
		if (m_Desc.dsvFormat != Format::UNKNOWN)
		{
			depthStencil =
				MakeDepthStencilState(m_Desc.renderState.depthStencilState, m_Desc.dsvFormat);
			rpDesc.depthStencil = &depthStencil;
		}

		m_Pipeline = device.CreateRenderPipeline(&rpDesc);
	}

	UniformLayoutEntry
	GraphicsPipeline::GetUniformLayoutEntry(std::string_view name) const noexcept
	{
		auto it = m_UniformLayoutEntries.find(name);
		if (it != m_UniformLayoutEntries.end())
			return it->second;

		gfatal("Uniform layout entry not found: {}", name);
	}

	std::vector<std::string>
	GraphicsPipeline::GetUniformBufferNames() const noexcept
	{
		auto names = std::vector<std::string>();
		names.reserve(m_UniformLayoutEntries.size());
		for (const auto& [name, entry] : m_UniformLayoutEntries) names.push_back(name);
		return names;
	}
}
