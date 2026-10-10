#pragma once
#include "gfx/DrawBucketTable.h"
#include <bgl/idl/DispatchArgs.h>
#include <bgpu/types/RasterState.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace bgl
{
	/**
	 * The colour-pass pixel program a bucket draws with: `programs.forward.<kind><layer>`, or
	 * `programs.forward.Grass_<kind>` for a grass bucket, which is always opaque. A terrain bucket
	 * draws with the mesh's: its stage emits the same vertex, and a terrain carries nothing a mesh
	 * does not. The impostor bucket draws with its stage's own, `programs.forward.Impostor`.
	 * @pre the bucket is not transparent -- the depth-sorted list draws through the one shared
	 * blend program, which no bucket owns.
	 */
	[[nodiscard]] std::string
	DrawBucketPixelSrc(const DrawBucketDesc& desc);

	/**
	 * The Ground Color pass's pixel program for a terrain bucket: `programs.forward.GroundColor_<kind>`,
	 * which writes the material's unlit base colour. @pre the bucket is a terrain's.
	 */
	[[nodiscard]] std::string
	DrawBucketGroundColorSrc(const DrawBucketDesc& desc);

	/** The amplification/mesh module for the bucket's stage. @pre the bucket is not transparent. */
	[[nodiscard]] std::string_view
	DrawBucketGeometrySrc(const DrawBucketDesc& desc);

	/**
	 * How a bucket's pipelines cull in hardware. A bucket that culls nothing leaves back faces to
	 * the mesh stage, which reads each material's doubleSided flag; only the materialless kNull
	 * and kAssert kinds cull in hardware, having no flag to read. Every pass drawing the bucket
	 * must agree, or its depth holds faces the colour pass never drew.
	 */
	[[nodiscard]] bgpu::RasterCullMode
	DrawBucketCullMode(const DrawBucketDesc& desc) noexcept;

	/**
	 * `expansionData.cullBackfaces` for the draw bucket: 1 where its pipeline culls nothing in
	 * hardware, so the mesh stage honours the material's doubleSided; 0 where the pipeline already
	 * culled and the mesh stage has nothing left to do.
	 */
	[[nodiscard]] uint32_t
	DrawBucketMeshStageCullsBackfaces(const DrawBucketDesc& desc) noexcept;

	/**
	 * Where a draw bucket's command count sits when its own dispatch args are the count buffer: the
	 * `threadCountX` that opens its entry -- first because every backend's indirect-argument layout
	 * puts X first -- in uint32s. Zero exactly when the bucket is empty, and
	 * the count verb clamps any other value to one command -- so the count is the grid, and a zero
	 * count can never be paired with a non-zero grid.
	 */
	[[nodiscard]] constexpr uint32_t
	DrawBucketCountIndex(const uint32_t bucket) noexcept
	{
		return bucket * static_cast<uint32_t>(sizeof(idl::DispatchArgs) / sizeof(uint32_t));
	}
}
