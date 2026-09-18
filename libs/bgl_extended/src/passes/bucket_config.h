#pragma once
#include "gfx/BucketTable.h"
#include "types/RasterState.h"
#include <string_view>

namespace bgl
{
	/**
	 * The colour-pass pixel program a bucket draws with.
	 * @pre the bucket is not transparent -- the depth-sorted list draws through the one shared
	 * blend program, which no bucket owns.
	 */
	[[nodiscard]] std::string_view
	BucketPixelSrc(const BucketDesc& desc);

	/** The amplification/mesh module for the bucket's tier. @pre the bucket is not transparent. */
	[[nodiscard]] std::string_view
	BucketGeometrySrc(const BucketDesc& desc);

	/**
	 * The discard-only twin of the bucket's pixel program, for the static depth pass: coverage is
	 * evaluated with the colour pass's own arithmetic, or the receiver would catch shadows on
	 * discarded texels. @pre a static-tier bucket on the kMask or kHashed layer.
	 */
	[[nodiscard]] std::string_view
	BucketCoveragePixelSrc(const BucketDesc& desc);

	/**
	 * How a bucket's pipelines cull in hardware. A bucket that culls nothing leaves back faces to
	 * the mesh stage, which reads each material's doubleSided flag; only the materialless kNull
	 * and kAssert kinds cull in hardware, having no flag to read. Every pass drawing the bucket
	 * must agree, or its depth holds faces the colour pass never drew.
	 */
	[[nodiscard]] RasterCullMode
	BucketCullMode(const BucketDesc& desc) noexcept;
}
