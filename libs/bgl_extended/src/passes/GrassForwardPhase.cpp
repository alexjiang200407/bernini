#include "passes/GrassForwardPhase.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "fg/PassDesc.h"
#include "passes/BindingNameCheck.h"
#include "passes/DrawData.h"
#include "passes/ForwardPhases.h"
#include "passes/SceneBindings.h"
#include "pipeline/MeshletKernel.h"
#include "scene/SceneView.h"
#include "scene/dispatch_limits.h"
#include "scene/scene_buffer_names.h"
#include "types/Barrier.h"
#include "types/MeshletState.h"
#include "uniforms/Uniforms.h"
#include <algorithm>
#include <array>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/WindDesc.h>
#include <bgl_common/gassert.h>
#include <core/math.h>
#include <cstdint>
#include <string_view>

namespace bgl
{
	namespace
	{
		// Keyed on the Slang global's name as reflection reports it, so this must track the
		// ConstantBuffer declaration in Grass.slang.
		constexpr auto c_Cbuffer = "grassData"sv;

		constexpr std::array<SceneBuffer, 5> c_GrassBuffers = {
			{ { c_GrassDrawsName,
			    "draws",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { c_GrassChunkRefsName,
			    "chunkRefs",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { c_GrassLookBufferName,
			    "looks",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { c_GrassChunkBufferName,
			    "chunks",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader },
			  { c_GrassClumpBufferName,
			    "clumps",
			    BarrierAccessFlag::kShaderResource,
			    BarrierSyncFlag::kVertexShader } }
		};

		constexpr std::array<std::string_view, 10> c_Fields = {
			"cameraPos"sv,     "pixelsPerUnit"sv,    "firstRef"sv,     "refCount"sv,
			"dispatchWidth"sv, "windDirection"sv,    "windStrength"sv, "windGustScale"sv,
			"windGustSpeed"sv, "windGustStrength"sv,
		};

		/**
		 * What one world unit spans on the render grid, in pixels, at a distance of one: half the
		 * grid's height times the projection's y scale, which is the length of the view-projection's
		 * y row since the view is a rotation.
		 */
		[[nodiscard]] float
		PixelsPerUnit(const DrawData& draw)
		{
			const glm::mat4& viewProj = draw.viewState.unjitteredViewProj;
			const float      yScale =
				glm::length(glm::vec3(viewProj[0][1], viewProj[1][1], viewProj[2][1]));
			return 0.5f * (draw.viewState.viewport.maxY - draw.viewState.viewport.minY) * yScale;
		}

		/** The wind's horizontal direction, unit; SetWind refused a direction without one. */
		[[nodiscard]] glm::vec2
		WindDirection(const WindDesc& wind)
		{
			return glm::normalize(glm::vec2(wind.direction.x, wind.direction.z));
		}

		[[nodiscard]] const SceneView&
		ViewOf(const DrawData& draw)
		{
			const auto* view = draw.view->As<SceneView>();
			gassert(view != nullptr, "The grass phase requires a bgl::SceneView");
			return *view;
		}
	}

	bool
	GrassForwardPhase::HasWork(const DrawData& draw) const
	{
		return !ViewOf(draw).GetGrassBatches().empty();
	}

	void
	GrassForwardPhase::Declare(PassDesc& desc) const
	{
		desc.AddRenderTarget(c_MotionVectorsName);
		for (const auto& binding : c_GrassBuffers)
		{
			desc.AddBufferArg(binding.graphName, binding.sync, binding.access);
		}
	}

	void
	GrassForwardPhase::Record(
		ForwardPhases&     kernels,
		MeshletState&      state,
		const DrawData&    draw,
		const PassContext& resources) const
	{
		ICommandList* cmd = resources.GetCommandList();
		gassert(cmd != nullptr, "Pass commandlist must be initialized");

		const SceneView& view = ViewOf(draw);
		const WindDesc&  wind = view.GetWind();

		for (const SceneView::GrassBatch& batch : view.GetGrassBatches())
		{
			MeshletKernel* kernel =
				kernels.BindDrawBucketKernel(batch.bucket, state, draw, resources);
			gassert(
				kernel != nullptr,
				"a grass batch's bucket was drawn before its kernel was built");
			if (kernel == nullptr || batch.refCount == 0)
			{
				continue;
			}

			// One amplification group per chunk reference, laid out in rows no wider than one
			// dispatch can launch, so a view past that many chunks still dispatches once.
			const uint32_t width = std::min(batch.refCount, c_MaxDispatchMeshGroups);
			const uint32_t rows  = core::div_ceil(batch.refCount, width);

			auto found = kernel->FindUniforms(c_Cbuffer);
			if (!found)
			{
				gfatal("Grass shader is missing its '{}' constant buffer", c_Cbuffer);
			}
			auto& uniforms = *found;
			BindSceneBuffers(uniforms, c_GrassBuffers, resources);
			uniforms["cameraPos"]     = draw.viewState.cameraPos;
			uniforms["pixelsPerUnit"] = PixelsPerUnit(draw);
			uniforms["firstRef"]      = batch.firstRef;
			uniforms["refCount"]      = batch.refCount;
			uniforms["dispatchWidth"] = width;

			uniforms["windDirection"]    = WindDirection(wind);
			uniforms["windStrength"]     = wind.strength;
			uniforms["windGustScale"]    = wind.gustScale;
			uniforms["windGustSpeed"]    = wind.gustSpeed;
			uniforms["windGustStrength"] = wind.gustStrength;

			cmd->SetMeshletState(state);
			cmd->DispatchMesh(width, rows, 1);
		}
	}

	void
	GrassForwardPhase::CheckBindings(BindingNameCheck& check)
	{
		check.Check(c_Cbuffer, GetUniformKeys(c_GrassBuffers)).Check(c_Cbuffer, c_Fields);
	}
}
