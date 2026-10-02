#pragma once
#include <bgl/GeomType.h>
#include <bgl/LodLevel.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceFlag.h>
#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl/idl/CullView.h>
#include <bgl/idl/InstanceLod.h>
#include <bgl/idl/MeshInstance.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <cstdint>
#include <optional>

namespace bgl
{
	/** The game slot a kind names -- its surface's registration index -- or empty for an engine kind. */
	[[nodiscard]] std::optional<uint32_t>
	GameSlot(MaterialType material) noexcept;

	/** The kind the records of the surface registered `slot`th carry. */
	[[nodiscard]] MaterialType
	GameSlotKind(uint32_t slot) noexcept;

	/**
	 * Which of a surface's textures its coverage is measured against, or empty for a surface that
	 * declares none.
	 *
	 * Hashed alpha relates a UV footprint to texels, so it needs one texture's resolution out of the
	 * eight a surface may bind -- and a surface answers coverage with arithmetic the engine cannot
	 * read the texture out of. The surface says which by the kind it declared the field as: a
	 * `CoverageSlot` is the claim itself, and a surface without one falls back to its first `ColorSlot`,
	 * where alpha rides in the colour exactly as it does on a PBR record.
	 *
	 * It supplies a resolution and nothing else: what `Coverage` samples is the surface's business,
	 * so a surface sampling a mask while declaring a larger colour is measured against the colour.
	 * That is what a `CoverageSlot` is for.
	 */
	[[nodiscard]] std::optional<uint32_t>
	CoverageCarrierSlot(const SurfaceParams& params) noexcept;

	/**
	 * Whether `geomType` can be drawn with `material`, which is what every door binding one to
	 * animated geometry checks. Static geometry takes anything; the animated tiers take every layer
	 * of a `kPBR` material and of a game surface's, and no other material type, having neither an
	 * unlit nor a loose variant.
	 *
	 * An invalid handle is rejected -- animated geometry has no unlit variant to fall back to.
	 */
	[[nodiscard]] bool
	AcceptsMaterial(GeomType geomType, MaterialHandle material) noexcept;

	/**
	 * Fills a placement's transform from an affine matrix. glm stores columns and the GPU reads
	 * rows, so the transpose is the packing -- and this is the only place that knows it, because a
	 * placement written the other way round draws correctly until something rotates or scales it.
	 *
	 * @param instance The placement to fill; only its transform is touched.
	 * @param transform An affine model-to-world matrix. Its fourth row is discarded, not checked.
	 */
	void
	WriteInstanceTransform(idl::MeshInstance& instance, const glm::mat4& transform) noexcept;

	/** Whether `flag`'s bit is set in the placement's flags word; the shaders' twin is in MeshInstance.slang. */
	[[nodiscard]] bool
	HasMeshInstanceFlag(const idl::MeshInstance& instance, MeshInstanceFlag flag) noexcept;

	/**
	 * Fills a placement's previous-frame transform, in the same packing WriteInstanceTransform uses.
	 *
	 * @param instance The placement to fill; only its prevTransform is touched.
	 * @param transform An affine model-to-world matrix. Its fourth row is discarded, not checked.
	 */
	void
	WriteInstancePrevTransform(idl::MeshInstance& instance, const glm::mat4& transform) noexcept;

	/** The affine matrix WriteInstanceTransform packed, with the implied fourth row restored. */
	[[nodiscard]] glm::mat4
	ReadInstanceTransform(const idl::MeshInstance& instance) noexcept;

	/**
	 * A placement's level of detail under one frustum, as idl::InstanceLod's Slang accessors read
	 * the word. The CPU never writes one -- the cull does -- but a readback has to decode it, and
	 * this reads the IDL's own shifts and masks rather than restating them.
	 */
	struct InstanceLodState
	{
		// Empty until a cull has chosen for the placement.
		std::optional<LodLevel> level;

		// The level being faded out of, empty while not fading.
		std::optional<LodLevel> outgoing;

		// Progress toward `level`, in [0, 1]; 1 on a placement not fading.
		float fade = 1.0f;

		// An automatic placement's sources: whether `level`'s entry, and `outgoing`'s, draw from
		// the rig's table rather than per instance. False on every other placement.
		bool fromTable         = false;
		bool outgoingFromTable = false;

		// Whether the pose pool granted the placement a per-instance pose for next frame.
		bool granted = false;
	};

	/** What the Slang accessors read out of `word`. */
	[[nodiscard]] InstanceLodState
	UnpackInstanceLod(idl::InstanceLod word) noexcept;

	/**
	 * Resolves a view's LodSelectionDesc into the cull view one draw uploads: the camera and the
	 * pixels a world unit spans, the threshold scale, the forced level (cLodForceNone when none),
	 * and how far a dissolve advances this frame. `frameSeconds` is the draw's clock step; a clock
	 * that did not advance -- a first frame, a paused one -- completes a dissolve at once rather
	 * than holding two levels on screen with no motion to hide them.
	 */
	void
	ResolveLodSelection(
		idl::CullView&          cullView,
		const LodSelectionDesc& selection,
		const glm::vec3&        cameraPos,
		float                   pixelsPerUnit,
		float                   frameSeconds) noexcept;
}
