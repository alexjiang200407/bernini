#include "passes/BlobShadowPhase.h"
#include "cmd/CommandList.h"
#include "device/Device.h"
#include "fg/PassDesc.h"
#include "passes/BinderNames.h"
#include "passes/DrawData.h"
#include "pipeline/MeshletPipeline.h"
#include "pipeline/PipelineBatch.h"
#include "resource/FrameBuffer.h"
#include "resource/Shader.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/BlendState.h"
#include "types/DepthStencilState.h"
#include "types/Format.h"
#include "types/MeshletState.h"
#include "types/RasterState.h"
#include "types/RenderState.h"
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/Viewport.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl_common/gassert.h>
#include <core/glm.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	namespace
	{
		constexpr auto c_Src = "programs.forward.BlobShadow"sv;

		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in BlobShadow.slang.
		constexpr auto c_Cbuffer = "gBlobShadowData"sv;

		// Every member Draw writes, kept beside the code that writes them so BinderNames catches
		// a shader rename at startup.
		constexpr std::array<std::string_view, 8> c_Fields = {
			"blobBuffer"sv, "meshBuffer"sv,  "palettes"sv,     "staticDepth"sv,
			"viewProj"sv,   "invViewProj"sv, "groundNormal"sv, "viewportRect"sv,
		};
	}

	void
	BlobShadowPhase::Init(IDevice* device, PipelineBatch& pipelines)
	{
		gassert(device != nullptr, "Device must be initialized");

		// The transparents' render state -- colour only, blended, depth read without write --
		// but its own two-stage program: the discs are not instance-pipeline geometry.
		auto pipelineDesc = MeshletPipelineDesc();

		pipelineDesc.meshShader  = device->CreateShader(std::string(c_Src), "MSMain");
		pipelineDesc.pixelShader = device->CreateShader(std::string(c_Src), "PSMain");

		pipelineDesc.AddRtvFormat(Format::RGBA16_FLOAT);
		pipelineDesc.SetDsvFormat(Format::D24S8);

		auto raster = RasterState();
		raster.SetFillMode(RasterFillMode::kSolid)
			.SetCullMode(RasterCullMode::kNone)
			.SetFrontCounterClockwise(true)
			.SetDepthClipEnable(true);

		// kLessOrEqual, not kLess: the pixel stage re-emits the receiver's sampled depth as
		// SV_Depth, and where the receiver itself is what the scene shows, the two are equal.
		auto depth = DepthStencilState{};
		depth.SetDepthTestEnable(true)
			.SetDepthWriteEnable(false)
			.SetDepthFunc(ComparisonFunc::kLessOrEqual)
			.SetStencilEnable(false);

		auto blend = BlendState{};
		blend.SetRenderTarget(
			0,
			BlendState::RenderTarget{}
				.EnableBlend()
				.SetSrcBlend(BlendFactor::kOne)
				.SetDestBlend(BlendFactor::kInvSrcAlpha)
				.SetBlendOp(BlendOp::kAdd)
				.SetSrcBlendAlpha(BlendFactor::kZero)
				.SetDestBlendAlpha(BlendFactor::kZero)
				.SetBlendOpAlpha(BlendOp::kAdd));

		pipelineDesc.renderState =
			RenderState().SetRasterState(raster).SetBlendState(blend).SetDepthStencilState(depth);

		pipelines.Add(m_Kernel, std::move(pipelineDesc));
	}

	void
	BlobShadowPhase::CheckBindings() const
	{
		BinderNames("BlobShadowPhase"sv, { &m_Kernel, 1 }).Check(c_Cbuffer, c_Fields);
	}

	void
	BlobShadowPhase::DeclareResources(PassDesc& desc)
	{
		// The palette arena is read too, for the soles; ForwardPass declares it with the skinned
		// tables, at the stage and access this reads it with.
		desc.AddBufferArg(
			BufferArg{ std::string(c_BlobShadowsName),
		               BarrierSyncFlag::kVertexShader,
		               BarrierAccessFlag::kShaderResource });

		desc.AddTextureArg(
			TextureArg{ std::string(c_StaticDepthName),
		                BarrierSyncFlag::kPixelShader,
		                BarrierAccessFlag::kShaderResource,
		                BarrierLayout::kShaderResource });
	}

	void
	BlobShadowPhase::Draw(const DrawData& draw, const PassContext& resources)
	{
		const auto* view = draw.view->As<SceneView>();
		gassert(view != nullptr, "BlobShadowPhase requires a bgl::SceneView");

		const uint32_t blobs = view->GetBlobShadowCount();
		if (blobs == 0)
		{
			return;
		}

		gassert(m_Kernel.pipeline.IsInitialized(), "Blob shadow pipeline must be initialized");

		if (auto found = m_Kernel.FindUniforms(c_Cbuffer))
		{
			auto& uniforms = *found;

			uniforms["blobBuffer"] = resources.GetBuffer(c_BlobShadowsName);
			uniforms["meshBuffer"] = resources.GetBuffer(c_MeshInstanceBufferName);
			uniforms["palettes"]   = resources.GetBuffer(c_BonePaletteName);
			uniforms["staticDepth"].SetIfValid(draw.targets.staticDepthSrv);
			uniforms["viewProj"]    = draw.viewState.viewProj;
			uniforms["invViewProj"] = glm::inverse(draw.viewState.viewProj);

			const GroundPlaneDesc& ground = view->GetScene()->As<Scene>()->GetGround();
			uniforms["groundNormal"]      = ground.normal;

			const Viewport& viewport = draw.viewState.viewport;
			uniforms["viewportRect"] = glm::vec4(
				viewport.minX,
				viewport.minY,
				1.0f / (viewport.maxX - viewport.minX),
				1.0f / (viewport.maxY - viewport.minY));
		}
		else
		{
			gfatal("Blob shadow shader is missing its '{}' constant buffer", c_Cbuffer);
		}

		// Colour only, exactly as the transparent phase binds: a blend PSO declares one
		// rtvFormat, so the velocity buffer must not be attached.
		auto gfxState = MeshletState();
		gfxState.viewportState.AddViewportAndScissorRect(draw.viewState.viewport);
		gfxState.frameBuffer = FrameBuffer()
		                           .AddColorAttachment(draw.targets.sceneColor)
		                           .SetDepthAttachment(draw.targets.depth);
		gfxState.kernel      = &m_Kernel;

		ICommandList* cmd = resources.GetCommandList();
		cmd->SetMeshletState(gfxState);
		cmd->DispatchMesh(blobs, 1, 1);
	}
}
