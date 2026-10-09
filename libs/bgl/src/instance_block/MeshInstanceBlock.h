#pragma once
#include "types/AutoRecord.h"
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/ToonShadingRigHandle.h>
#include <bgpu/buffer/EntryBuffer.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <cstdint>
#include <optional>

namespace bgl
{
	/** A view's instance block: its run of the mesh buffer, its writer and its own kernel. */
	struct MeshInstanceBlock
	{
		MeshInstanceBlock() noexcept                    = default;
		MeshInstanceBlock(MeshInstanceBlock&&) noexcept = default;
		MeshInstanceBlock(const MeshInstanceBlock&)     = delete;

		MeshInstanceBlock&
		operator=(MeshInstanceBlock&&) noexcept = default;

		MeshInstanceBlock&
		operator=(const MeshInstanceBlock&) = delete;

		~MeshInstanceBlock() noexcept = default;

		GeomHandle            geom;
		uint32_t              capacity = 0;
		MeshInstanceWriterRef writer;

		// Its run of the view's MeshInstance buffer: slot i is element range.first + i.
		bgpu::EntryRange range;

		// A skinned block's one kAuto record and foot-IK record, which every slot's MeshInstance
		// names and none owns; both null on a static block.
		AutoRecord shared;

		// The writer's pipeline with a constant buffer of the block's own, so two blocks sharing a
		// writer keep their parameters apart. Empty while `writer` is.
		bgpu::ComputeKernel kernel;

		// Shared by every placement of the block, which holds one use of it until it is deleted.
		ToonShadingRigHandle toonShadingRig;

		// Cast by every placement of the block its writer shows, as MeshInstanceBlockDesc says.
		std::optional<BlobShadowDesc> blobShadow;
	};
}
