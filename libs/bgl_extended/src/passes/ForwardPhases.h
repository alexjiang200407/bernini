#pragma once
#include "gfx/DrawBucketTable.h"
#include "passes/BucketedForwardPhase.h"
#include "passes/GrassForwardPhase.h"
#include "passes/PassInitContext.h"
#include "passes/TransparentForwardPhase.h"
#include "pipeline/MeshletKernel.h"
#include "types/DrawBucketMask.h"
#include "types/MeshletState.h"
#include <concepts>
#include <cstdint>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>
#include <vector>

namespace bgl
{
	class PipelineBatch;
	class ForwardPhases;

	class IDevice;
	class FrameGraph;
	class PassContext;

	struct DrawData;
	struct MeshletState;
	struct PassDesc;

	/**
	 * Which part of the forward render a pass records, in the order they draw. After `kGrass` the
	 * depth holds the world alone -- the static tier's opaque surfaces and their grass, moving or
	 * not: the blob-shadow pass draws there, and it is where an HZB build belongs.
	 */
	enum class ForwardPhase : uint8_t
	{
		kWorld,        // the static tier's non-transparent buckets
		kGrass,        // the grass the view's geoms grow
		kSkinned,      // the skinned tier's non-transparent buckets
		kTransparent,  // the depth-sorted list, every tier
	};

	/**
	 * One graph pass of the forward render: which draws it records and how it dispatches them. The
	 * kernels, the uniforms every forward kernel shares and the targets are ForwardPhases'; a phase
	 * declares only what its own dispatch reads, and records with kernels handed to it bound.
	 */
	template <class Phase>
	concept ForwardPhaseRecorder = requires(
		const Phase&       phase,
		PassDesc&          desc,
		ForwardPhases&     kernels,
		MeshletState&      state,
		const DrawData&    draw,
		const PassContext& resources) {
		{ phase.Name() } -> std::convertible_to<std::string_view>;
		{ phase.HasWork(draw) } -> std::same_as<bool>;
		phase.Declare(desc);
		phase.Record(kernels, state, draw, resources);
	};

	/**
	 * The one owner of every kernel feeding the forward pixel programs -- one per draw bucket, and
	 * the shared blend kernel -- and of the uniforms and targets they all share. Attached to the
	 * graph as one pass per ForwardPhase, each recorded by a phase it composes. The set is fixed and
	 * ordered: the frame's order is RenderContext's, and Blob Shadows draws between two of them.
	 */
	class ForwardPhases
	{
	public:
		ForwardPhases() = default;
		~ForwardPhases() noexcept { logger::trace("~ForwardPhases"); }

		ForwardPhases(const ForwardPhases&) noexcept = delete;
		ForwardPhases(ForwardPhases&&) noexcept      = delete;

		ForwardPhases&
		operator=(const ForwardPhases&) noexcept = delete;

		ForwardPhases&
		operator=(ForwardPhases&&) noexcept = delete;

		void
		Release()
		{
			for (MeshletKernel& kernel : m_Kernels)
			{
				kernel.Reset();
			}
			m_TransparentKernel.Reset();
		}

		void
		Init(const PassInitContext& ctx);

		/**
		 * Requests the kernels for the buckets set in `buckets` that are not already initialized;
		 * they are live once `pipelines` is built. A bucket already initialized is left alone.
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

		/** @pre the batch Init requested into has been built. Fatal on a binder name no PSO declares. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw, ForwardPhase phase);

		[[nodiscard]] const DrawBucketTable&
		DrawBuckets() const noexcept
		{
			return *m_DrawBucketTable;
		}

		/**
		 * The bucket's kernel with the uniforms every forward kernel shares bound for this draw, set
		 * into `state` with the targets it declares -- colour, velocity and depth. Null, and `state`
		 * untouched, while it is unbuilt.
		 */
		[[nodiscard]] MeshletKernel*
		BindDrawBucketKernel(
			uint32_t           bucket,
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources);

		/**
		 * The shared blend kernel, bound as BindDrawBucketKernel binds, with colour and depth alone:
		 * a blended surface has no single depth to reproject, so its kernel declares no velocity.
		 */
		[[nodiscard]] MeshletKernel*
		BindTransparentKernel(
			MeshletState&      state,
			const DrawData&    draw,
			const PassContext& resources);

	private:
		template <ForwardPhaseRecorder Phase>
		void
		AttachPhase(FrameGraph& fg, const DrawData& draw, const Phase& phase);

		template <ForwardPhaseRecorder Phase>
		void
		Execute(const Phase& phase, const DrawData& draw, const PassContext& resources);

		/** Binds the geometry, material, and IBL uniforms common to every forward draw. */
		void
		BindKernel(MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		/** The binder-name check over one kernel family; a family with nothing built is skipped. */
		void
		CheckKernelNames(std::span<const MeshletKernel> kernels) const;

		// Indexed by bucket id, grown to the table's count as buckets are demanded.
		std::vector<MeshletKernel> m_Kernels;

		// The shared blend kernel (see DrawTransparent); no bucket owns it.
		MeshletKernel m_TransparentKernel;

		const DrawBucketTable* m_DrawBucketTable = nullptr;

		BucketedForwardPhase    m_World{ GeometryStage::kStaticMesh, "World" };
		BucketedForwardPhase    m_Skinned{ GeometryStage::kSkinnedMesh, "Skinned" };
		GrassForwardPhase       m_Grass;
		TransparentForwardPhase m_Transparent;
	};
}
