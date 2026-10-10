#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/BucketedForwardPhase.h"
#include "passes/GrassForwardPhase.h"
#include "passes/PassInitContext.h"
#include "passes/TerrainForwardPhase.h"
#include "passes/TransparentForwardPhase.h"
#include "passes/WaterForwardPhase.h"
#include "types/DrawBucketMask.h"
#include <bgl/SurfaceType.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/types/MeshletState.h>
#include <cstdint>
#include <optional>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>
#include <vector>

namespace bgpu
{
	class PipelineBatch;
	class IDevice;
	class IResourceManager;
	struct MeshletState;
}

namespace bgl
{
	class ForwardPhases;

	class FrameGraph;
	class PassContext;

	struct DrawData;
	struct PassDesc;

	/**
	 * Which part of the forward render a pass records, in the order they draw. After `kGrass` the
	 * depth holds the world alone -- the terrain, the static tier's opaque surfaces and their
	 * grass, moving or not: the blob-shadow pass draws there, and it is where an HZB build belongs.
	 */
	enum class ForwardPhase : uint8_t
	{
		kTerrain,   // the scene's terrains: the ground goes down first, the largest occluder
		kWorld,     // the static tier's non-transparent buckets
		kImpostor,  // every placement drawn past its last level, as its geom's impostor
		kWorldPhase2,  // the static tier's buckets again: what the occlusion cull's phase 2 found visible
		kGrass,        // the grass the view's geoms grow
		kSkinned,      // the skinned tier's non-transparent buckets
		kWater,        // the static tier's water buckets, over the depth the phases above wrote
		kTransparent,  // the depth-sorted list, every tier
	};

	/**
	 * Which of a draw bucket's two lanes a dispatch draws (idl::cDissolveLane): the placements at
	 * rest, or those dissolving between two levels of detail, through pipelines whose pixel programs
	 * carry the dither. Only a mesh stage's bucket has the second; a grass blade never dissolves.
	 */
	enum class DrawLane : uint8_t
	{
		kAtRest,
		kDissolve,
	};

	/** Whether the bucket's placements can dissolve, and so whether it owns a kDissolve kernel. */
	[[nodiscard]] bool
	DrawBucketDissolves(const DrawBucketDesc& desc) noexcept;

	/**
	 * The one owner of every kernel feeding the forward pixel programs -- one per lane of a
	 * mesh-stage draw bucket, one per grass bucket, and the shared blend kernel -- and of the
	 * uniforms and targets they all share. Attached to the graph as one pass per ForwardPhase, each
	 * recorded by a phase it composes. The set is fixed and ordered: the frame's order is
	 * RenderContext's, and Blob Shadows draws between two of them.
	 */
	class ForwardPhases
	{
	public:
		explicit ForwardPhases(const PassInitContext& ctx);
		~ForwardPhases() noexcept { spdlog::trace("~ForwardPhases"); }

		ForwardPhases(const ForwardPhases&) noexcept = delete;
		ForwardPhases(ForwardPhases&&) noexcept      = delete;

		ForwardPhases&
		operator=(const ForwardPhases&) noexcept = delete;

		ForwardPhases&
		operator=(ForwardPhases&&) noexcept = delete;

		/**
		 * Requests the kernels for the buckets set in `buckets` that are not already initialized --
		 * both lanes' where the bucket dissolves; they are live once `pipelines` is built. A bucket
		 * already initialized is left alone.
		 * @pre every set bit is an allocated, non-transparent bucket.
		 */
		void
		AddDrawBucketKernels(const PassInitContext& ctx, const DrawBucketMask& demanded);

		/**
		 * Requests the one shared blend kernel the whole depth-sorted list draws through --
		 * demanded by any transparent bucket, owned by none.
		 */
		void
		AddTransparentKernel(const PassInitContext& ctx);

		/**
		 * The registered surfaces' contracts, indexed by slot. A toon character's buckets draw
		 * through MSToon at rest, whose vertices carry the placement's toon shading rig block; a
		 * water surface's draw in Forward Water, through pipelines that read the depth rather than
		 * attach it. @pre called before any bucket kernel is requested.
		 */
		void
		SetSurfaceShading(std::vector<SurfaceShading> slots);

		/** Whether `bucket` draws a water surface, and so in Forward Water alone. */
		[[nodiscard]] bool
		IsWaterBucket(uint32_t bucket) const noexcept;

		/**
		 * Whether `draw`'s view has ever placed an instance in a water bucket: the question that
		 * decides whether the frame copies scene colour for Forward Water to refract through.
		 */
		[[nodiscard]] bool
		DemandsWater(const DrawData& draw) const;

		[[nodiscard]] bool
		DrawBucketInitialized(uint32_t bucket) const noexcept
		{
			return bucket < m_Kernels.size() && m_Kernels[bucket].pipeline.IsInitialized();
		}

		[[nodiscard]] bool
		TransparentInitialized() const noexcept
		{
			return m_TransparentKernel.pipeline.IsInitialized();
		}

		/** @pre the constructor's batch has been built. Fatal on a binder name no PSO declares. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, ForwardPhase phase);

		/**
		 * Ground Color: clears the view's ground-colour texture and draws every terrain into it from
		 * above, through each terrain bucket's albedo program. `draw` is the top-down draw -- its
		 * viewProj and cull frustum the texture's square, its camera position and pixels per unit
		 * the view's, so the patches near the camera are as fine as the colour pass draws them.
		 * Attaches nothing when the view has no texture to fill.
		 */
		void
		AttachGroundColor(
			FrameGraph&             fg,
			const DrawData&         draw,
			bgpu::IResourceManager* resourceManager);

		[[nodiscard]] const DrawBucketTable&
		DrawBuckets() const noexcept
		{
			return *m_DrawBucketTable;
		}

		/**
		 * The bucket's kernel for `lane` with the uniforms every forward kernel shares bound for this
		 * draw, set into `state` with the targets it declares -- colour, velocity and depth, which a
		 * water bucket reads rather than attaches. Null,
		 * and `state` untouched, while it is unbuilt, and for a lane the bucket does not have.
		 */
		[[nodiscard]] bgpu::MeshletKernel*
		BindDrawBucketKernel(
			uint32_t            bucket,
			DrawLane            lane,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources);

		/**
		 * The terrain bucket's albedo kernel, bound as BindDrawBucketKernel binds, with the view's
		 * ground-colour texture as its one target. Null while it is unbuilt.
		 */
		[[nodiscard]] bgpu::MeshletKernel*
		BindGroundColorKernel(
			uint32_t            bucket,
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources);

		/**
		 * The shared blend kernel, bound as BindDrawBucketKernel binds, with colour and depth alone:
		 * a blended surface has no single depth to reproject, so its kernel declares no velocity.
		 */
		[[nodiscard]] bgpu::MeshletKernel*
		BindTransparentKernel(
			bgpu::MeshletState& state,
			const DrawData&     draw,
			const PassContext&  resources);

	private:
		[[nodiscard]] const IForwardPhase&
		Phase(ForwardPhase phase) const noexcept;

		void
		Execute(const IForwardPhase& phase, const DrawData& draw, const PassContext& resources);

		/** Binds the geometry, material, and IBL uniforms common to every forward draw. */
		void
		BindKernel(bgpu::MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		/** The binder-name check over one kernel family; a family with nothing built is skipped. */
		void
		CheckKernelNames(std::span<const bgpu::MeshletKernel> kernels) const;

		// Indexed by bucket id, grown to the table's count as buckets are demanded; the dissolve
		// lane's stays unbuilt for a bucket that does not dissolve.
		std::vector<bgpu::MeshletKernel> m_Kernels;
		std::vector<bgpu::MeshletKernel> m_DissolveKernels;
		// A terrain bucket's albedo kernel, for Ground Color; uninitialized for every other bucket.
		std::vector<bgpu::MeshletKernel> m_GroundColorKernels;

		// The shared blend kernel (see DrawTransparent); no bucket owns it.
		bgpu::MeshletKernel m_TransparentKernel;

		/** The contract of the surface `material` names, empty for an engine kind. */
		[[nodiscard]] std::optional<SurfaceShading>
		ShadingOf(MaterialType material) const noexcept;

		const DrawBucketTable*      m_DrawBucketTable = nullptr;
		std::vector<SurfaceShading> m_SurfaceShading;

		BucketedForwardPhase    m_World{ GeometryStage::kStaticMesh, "World" };
		BucketedForwardPhase    m_WorldPhase2{ GeometryStage::kStaticMesh, "World Phase 2" };
		BucketedForwardPhase    m_Skinned{ GeometryStage::kSkinnedMesh, "Skinned" };
		BucketedForwardPhase    m_Impostor{ GeometryStage::kImpostor, "Impostor" };
		TerrainForwardPhase     m_Terrain;
		GrassForwardPhase       m_Grass;
		TransparentForwardPhase m_Transparent;
		WaterForwardPhase       m_Water{ *this };
	};
}
