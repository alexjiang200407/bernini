#include "passes/TerrainForwardPhase.h"
#include "fg/PassDesc.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "passes/SceneBindings.h"
#include "scene/SceneView.h"
#include "scene/TextureAssetStore.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/idl/Constants.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/math.h>
#include <cstdint>
#include <string_view>

namespace bgl
{
	namespace
	{
		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in TerrainData.slang.
		constexpr auto c_Cbuffer = "terrainData"sv;

		constexpr std::array<SceneBuffer, 2> c_TerrainBuffers = {
			{ { c_TerrainBufferName,
			    "terrains",
			    bgpu::BarrierAccessFlag::kShaderResource,
			    bgpu::BarrierSyncFlag::kVertexShader },
			  { c_TerrainNodeBoundsBufferName,
			    "nodeBounds",
			    bgpu::BarrierAccessFlag::kShaderResource,
			    bgpu::BarrierSyncFlag::kVertexShader } }
		};

		constexpr std::array<std::string_view, 10> c_Fields = {
			"heights"sv,       "heightSampler"sv, "frustumPlanes"sv, "cameraPos"sv,
			"pixelsPerUnit"sv, "lodPixelScale"sv, "terrain"sv,       "firstNodeBound"sv,
			"nodeCount"sv,     "dispatchWidth"sv,
		};

		[[nodiscard]] const SceneView&
		ViewOf(const DrawData& draw)
		{
			const auto* view = draw.view->As<SceneView>();
			core::ensure(view != nullptr, "The terrain phase requires a bgl::SceneView");
			return *view;
		}
	}

	bool
	TerrainForwardPhase::HasWork(const DrawData& draw) const
	{
		return !ViewOf(draw).GetTerrainBatches().empty();
	}

	void
	TerrainForwardPhase::Declare(PassDesc& desc) const
	{
		desc.AddRenderTarget(c_MotionVectorsName);
		for (const auto& binding : c_TerrainBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}
	}

	void
	TerrainForwardPhase::Record(
		ForwardPhases&      kernels,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources) const
	{
		bgpu::ICommandList* cmd = resources.GetCommandList();
		core::ensure(cmd != nullptr, "Pass commandlist must be initialized");

		for (const SceneView::TerrainBatch& batch : ViewOf(draw).GetTerrainBatches())
		{
			bgpu::MeshletKernel* kernel =
				kernels
					.BindDrawBucketKernel(batch.bucket, DrawLane::kAtRest, state, draw, resources);
			core::ensure(
				kernel != nullptr,
				"a terrain's bucket was drawn before its kernel was built");
			if (kernel == nullptr || batch.nodeCount == 0)
			{
				continue;
			}

			// One amplification group per node, laid out in rows no wider than one dispatch can
			// launch, so a terrain past that many nodes still dispatches once.
			const uint32_t width = std::min(batch.nodeCount, idl::cMaxDispatchMeshGroups);
			const uint32_t rows  = core::div_ceil(batch.nodeCount, width);

			auto found = kernel->FindUniforms(c_Cbuffer);
			if (!found)
			{
				core::fatal("Terrain shader is missing its '{}' constant buffer", c_Cbuffer);
			}
			auto& uniforms = *found;
			BindSceneBuffers(uniforms, c_TerrainBuffers, resources);
			uniforms["heights"]       = batch.heights;
			uniforms["heightSampler"] = draw.samplers.linearClamp;
			for (uint32_t plane = 0; plane < 6; ++plane)
			{
				uniforms["frustumPlanes"][plane] = draw.viewState.cullView.frustumPlanes[plane];
			}
			uniforms["cameraPos"]      = draw.viewState.cameraPos;
			uniforms["pixelsPerUnit"]  = draw.viewState.pixelsPerUnit;
			uniforms["lodPixelScale"]  = draw.viewState.cullView.lodPixelScale;
			uniforms["terrain"]        = batch.record;
			uniforms["firstNodeBound"] = batch.firstNodeBound;
			uniforms["nodeCount"]      = batch.nodeCount;
			uniforms["dispatchWidth"]  = width;

			cmd->SetMeshletState(state);
			cmd->DispatchMesh(width, rows, 1);
		}
	}

	void
	TerrainForwardPhase::CheckBindings(BindingNameCheck& check)
	{
		check.Check(c_Cbuffer, GetUniformKeys(c_TerrainBuffers)).Check(c_Cbuffer, c_Fields);
	}
}
