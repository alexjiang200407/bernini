#pragma once

#include <array>
#include <bgl/GeomHandle.h>
#include <bgl/IScene.h>
#include <bgl/MaterialHandle.h>
#include <cstdint>

/**
 * A rig synthesized from scratch, so a test needs no `.bmesh`/`.bskel`/`.banim`: one ready-made
 * sliding quad, for the cases that need geometry whose *vertices* move rather than a camera that
 * does.
 */
namespace bgl::test::skinned_synth
{
	// The sliding quad's two clips, over one sample pool; see AddSlidingQuadGeom.
	constexpr uint32_t c_LoopClip  = 0;
	constexpr uint32_t c_ClampClip = 1;

	// 30 fps: RenderJob::time t at rate 1 is frame 30 * t.
	constexpr float c_SampleRate = 30.0f;

	// The X the quad has moved by at frame 1, in the quad's own units.
	constexpr float c_Step = 1.0f;

	// Corner order: bottom-left, bottom-right, top-left, top-right.
	extern const std::array<glm::vec3, 4> c_QuadAtOrigin;

	/**
	 * The sliding quad: a 4-vertex quad on [-1, 1]², every vertex bound to one bone at full weight,
	 * so a pose is readable as an offset -- a quad at offset d covers [d-1, d+1]. Translation only,
	 * so nothing here depends on how rotations interpolate.
	 *
	 * Two clips, each shaped the way the importer writes its kind, because playback's wrap depends
	 * on that shape (see clip_playback.slang):
	 *
	 * - `c_LoopClip`, frames [origin, +step, origin]. Three frames, the last repeating the first --
	 *   which is what marks a clip looping -- so a cycle is *two* intervals.
	 * - `c_ClampClip`, frames [origin, +step]. A one-shot ends where it ends, so nothing is
	 *   duplicated.
	 *
	 * Adds the rig and registers the quad against it, drawn with `material`, which must be opaque
	 * PBR -- what AddSkinnedMeshGeom demands. The rig is the scene's for as long as the geom is; a
	 * caller that never deletes the geom never has to name it, which is why only the geom comes
	 * back.
	 */
	[[nodiscard]] GeomHandle
	AddSlidingQuadGeom(IScene& scene, MaterialHandle material);
}
