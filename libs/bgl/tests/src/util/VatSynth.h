#pragma once

#include <assetlib_structs/ImageData.h>
#include <bgl/IScene.h>

/**
 * VAT texture pairs synthesized from scratch, so a test needs no `.bvat`: the primitives a bake
 * would have written, and one ready-made rig built from them.
 */
namespace bgl::test::vat_synth
{
	/** A single-mip `columns` x `rows` texture of `texelBytes` per texel, zero-filled. */
	[[nodiscard]] assetlib::ImageData
	MakeImage(uint32_t columns, uint32_t rows, assetlib::VkFormat format, uint32_t texelBytes);

	/** Writes `corners` across row `row` of a position texture, unorm-packed in the bounds. */
	void
	WritePositionRow(
		assetlib::ImageData&       image,
		uint32_t                   row,
		std::span<const glm::vec3> corners,
		glm::vec3                  boundsMin,
		glm::vec3                  boundsMax);

	/** A normal texture of +Z and no twist everywhere, for geometry that only translates in its plane. */
	[[nodiscard]] assetlib::ImageData
	MakeFlatNormalTexture(uint32_t columns, uint32_t rows);

	// The sliding quad's two clips, over the same rows; see AddSlidingQuadGeom.
	constexpr uint32_t c_LoopClip  = 0;
	constexpr uint32_t c_ClampClip = 1;

	// 30 fps: RenderJob::time t at rate 1 is frame 30 * t.
	constexpr float c_SampleRate = 30.0f;

	// The X the quad has moved by at frame 1, in the quad's own units.
	constexpr float c_Step = 1.0f;

	// Corner order (also column order): bottom-left, bottom-right, top-left, top-right.
	extern const std::array<glm::vec3, 4> c_QuadAtOrigin;

	/**
	 * The sliding quad: a 4-vertex quad on [-1, 1]² that translates `c_Step` in X, so a pose is
	 * readable as an offset -- a quad at offset d covers [d-1, d+1].
	 *
	 * Two clips, each shaped the way the importer writes its kind, because playback's wrap depends
	 * on that shape (see clip_playback.slang):
	 *
	 * - `c_LoopClip`, rows 0-2 as [origin, +step, origin] and row 3 the pad. Three frames, the last
	 *   repeating the first -- which is what marks a clip looping -- so a cycle is *two* intervals.
	 * - `c_ClampClip`, rows 4-5 as [origin, +step] and row 6 the pad. A one-shot ends where it ends,
	 *   so nothing is duplicated.
	 *
	 * Each pad duplicates its clip's *last* frame, exactly as the bake writes it; it exists to stop
	 * fractional-frame blending bleeding into the clip stacked below, and playback never reads it.
	 *
	 * Uploads the pair and registers the quad as a VAT geom with both clips, drawn with
	 * `material`, which must be opaque PBR -- what AddVatMeshGeom demands.
	 */
	[[nodiscard]] GeomHandle
	AddSlidingQuadGeom(IScene& scene, MaterialHandle material);
}
