#include "passes/WaterForwardPhase.h"
#include "fg/PassDesc.h"
#include "gfx/DrawBucketTable.h"
#include "gfx/frame_constants.h"
#include "passes/BindingNameCheck.h"
#include "passes/BucketedForwardPhase.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "scene/SceneView.h"
#include "scene/TextureAssetStore.h"
#include "scene/scene_buffer_names.h"
#include <algorithm>
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/types/Viewport.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstdint>
#include <span>
#include <string_view>

namespace bgl
{
	namespace
	{
		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in WaterData.slang.
		constexpr auto c_Cbuffer = "waterData"sv;

		// lib.forward.WaterData's cMaxWaterTerrains, and the four heights fields it names.
		constexpr uint32_t                                         c_MaxWaterTerrains = 4;
		constexpr std::array<std::string_view, c_MaxWaterTerrains> c_HeightsFields    = {
			"terrainHeights0"sv,
			"terrainHeights1"sv,
			"terrainHeights2"sv,
			"terrainHeights3"sv,
		};

		constexpr std::array<std::string_view, 9> c_Fields = {
			"sceneDepth"sv,   "sceneColor"sv,     "invViewProj"sv, "viewportRect"sv,  "time"sv,
			"terrainCount"sv, "terrainRecords"sv, "terrains"sv,    "heightSampler"sv,
		};

		[[nodiscard]] const SceneView&
		ViewOf(const DrawData& draw)
		{
			const auto* view = draw.view->As<SceneView>();
			core::ensure(view != nullptr, "The water phase requires a bgl::SceneView");
			return *view;
		}

	}

	void
	WaterForwardPhase::Bind(
		bgpu::MeshletKernel& kernel,
		const DrawData&      draw,
		const PassContext&   resources)
	{
		auto found = kernel.FindUniforms(c_Cbuffer);
		if (!found)
		{
			return;
		}
		auto& uniforms = *found;

		uniforms["sceneDepth"]  = draw.targets.depthSrv;
		uniforms["sceneColor"]  = draw.targets.waterCopy.copySrv;
		uniforms["invViewProj"] = glm::inverse(draw.viewState.viewProj);

		const bgpu::Viewport& viewport = draw.viewState.viewport;
		uniforms["viewportRect"]       = glm::vec4(
			viewport.minX,
			viewport.minY,
			1.0f / (viewport.maxX - viewport.minX),
			1.0f / (viewport.maxY - viewport.minY));
		uniforms["time"] = draw.clock.time;

		const std::span<const SceneView::TerrainBatch> batches = ViewOf(draw).GetTerrainBatches();
		const auto                                     count =
			static_cast<uint32_t>(std::min<size_t>(batches.size(), c_MaxWaterTerrains));

		auto records = glm::uvec4(0u);
		for (uint32_t slot = 0; slot < count; ++slot)
		{
			records[static_cast<glm::length_t>(slot)] = batches[slot].record;
			uniforms[c_HeightsFields[slot]]           = batches[slot].heights;
		}
		uniforms["terrainCount"]   = count;
		uniforms["terrainRecords"] = records;
		uniforms["terrains"]       = resources.GetBuffer(c_TerrainBufferName);
		uniforms["heightSampler"]  = draw.samplers.linearClamp;
	}

	bool
	WaterForwardPhase::HasWork(const DrawData& draw) const
	{
		// No copy is a pool that refused it, and water is then skipped rather than refracting
		// through a null view.
		return !draw.targets.waterCopy.copySrv.IsNull() && m_Kernels.DemandsWater(draw);
	}

	void
	WaterForwardPhase::Declare(PassDesc& desc, const DrawData& /*draw*/) const
	{
		desc.AddRenderTarget(c_MotionVectorsName)
			.AddIndirectArgs(c_CompactDispatchArgsName)
			.AddTextureRead(c_DepthName, bgpu::BarrierSyncFlag::kPixelShader)
			.AddTextureRead(c_SceneColorCopyName, bgpu::BarrierSyncFlag::kPixelShader)
			.AddBufferRead(c_TerrainBufferName, bgpu::BarrierSyncFlag::kPixelShader);
	}

	void
	WaterForwardPhase::Record(
		ForwardPhases&      kernels,
		bgpu::MeshletState& state,
		const DrawData&     draw,
		const PassContext&  resources) const
	{
		const DrawBucketTable& table = kernels.DrawBuckets();
		for (uint32_t bucket = 0, count = table.Count(); bucket < count; ++bucket)
		{
			if (!kernels.IsWaterBucket(bucket))
			{
				continue;
			}
			RecordDrawBucket(kernels, state, draw, resources, bucket);
		}
	}

	void
	WaterForwardPhase::CheckBindings(BindingNameCheck& check)
	{
		check.Check(c_Cbuffer, c_Fields).Check(c_Cbuffer, c_HeightsFields);
	}
}
