#pragma once
#include "types/Format.h"
#include "types/FormatInfo.h"
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl_common/idl/MeshInstance.h>
#include <bgl_common/idl/PsoType.h>
#include <cstdint>
#include <optional>

namespace bgl
{
	FormatInfo
	GetFormatInfo(Format format);

	idl::PsoType
	GetPsoFromGeomAndMaterial(GeomType geom, MaterialType material, LayerType layer);

	/** The reserved game slot a kind names, or empty for a kind that is not a slot's. */
	[[nodiscard]] std::optional<uint32_t>
	GameSlot(MaterialType material) noexcept;

	/** The kind a reserved game slot's records carry. @pre slot < cGameSlots. */
	[[nodiscard]] MaterialType
	GameSlotKind(uint32_t slot) noexcept;

	/**
	 * The first of `slot`'s rows. @pre slot < cGameSlots.
	 *
	 * constexpr because ForwardPass's PSO table is built at compile time, which is also what holds
	 * the table's order to PsoType's.
	 */
	[[nodiscard]] constexpr uint32_t
	GameSlotRowBase(const uint32_t slot) noexcept
	{
		return static_cast<uint32_t>(idl::PsoType::kGameRowsStart) + slot * idl::cGameSlotRows;
	}

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

	/** Whether `pso` is one of the reserved game slots' rows at all. */
	[[nodiscard]] bool
	IsGameRow(uint32_t pso) noexcept;

	/** Which of its slot's rows `pso` is. @pre IsGameRow(pso). */
	[[nodiscard]] uint32_t
	GameRowOffset(uint32_t pso) noexcept;

	/**
	 * A slot's row for a geometry tier and a layer, from its first row. Opaque, alpha-test and
	 * hashed are per tier, since their geometry stage is the tier's own; blended is one row both
	 * tiers share, because the blended pipeline's geometry stage branches tier per instance.
	 *
	 * A tier that is neither static nor skinned is bgl's own bug here, as is a layer that is
	 * none of the four.
	 */
	[[nodiscard]] idl::PsoType
	GameSlotRow(uint32_t slot, GeomType geom, LayerType layer);

	/**
	 * The PSO bucket for `SubmeshInstance::pso`. An invalid handle resolves to the unlit `kNull`
	 * material, so a submesh that names no material renders flat rather than failing to load.
	 */
	uint32_t
	SubmeshPso(GeomType geomType, MaterialHandle material);

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
	 * Whether `pso` draws with alpha blending. Its instances are excluded from the PSO-bucketed
	 * counting sort and drawn from a separate depth-sorted list instead, since blending order is
	 * depth-first, not PSO-first. Mirrored by TransparentDepthKeys.slang, which keys that list.
	 */
	bool
	IsTransparentPso(uint32_t pso) noexcept;

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
