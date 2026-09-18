#pragma once
#include "types/Format.h"
#include "types/FormatInfo.h"
#include <bgl/GeomType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/MeshInstanceFlag.h>
#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl_common/idl/MeshInstance.h>
#include <cstdint>
#include <optional>

namespace bgl
{
	FormatInfo
	GetFormatInfo(Format format);

	/** The reserved game slot a kind names, or empty for a kind that is not a slot's. */
	[[nodiscard]] std::optional<uint32_t>
	GameSlot(MaterialType material) noexcept;

	/** The kind a reserved game slot's records carry. @pre slot < cGameSlots. */
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
}
