#pragma once
#include "types/Format.h"
#include "types/FormatInfo.h"
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/PsoType.h>

namespace bgl
{
	FormatInfo
	GetFormatInfo(Format format);

	PsoType
	GetPsoFromGeomAndMaterial(GeomType geom, MaterialType material, LayerType layer);

	/**
	 * The PSO bucket for `SubmeshInstance::pso`. An invalid handle resolves to the unlit `kNull`
	 * material, so a submesh that names no material renders flat rather than failing to load.
	 */
	uint32_t
	SubmeshPso(GeomType geomType, MaterialHandle material);

	/**
	 * Whether `geomType` can be drawn with `material`, which is what every door binding one to
	 * animated geometry checks. Static geometry takes every layer; the skinned pipeline adds cutout
	 * and hashed to opaque, since both discard rather than blend and so stay in the opaque bucket;
	 * VAT has opaque alone. Only blending is missing from the skinned set, and it is the one that
	 * would need the depth-sorted list.
	 *
	 * An invalid handle is rejected -- animated geometry has no unlit variant to fall back to.
	 */
	[[nodiscard]] bool
	AcceptsMaterial(GeomType geomType, MaterialHandle material) noexcept;

	/**
	 * Whether `pso` draws with alpha blending. Its instances are excluded from the PSO-bucketed
	 * counting sort and drawn from a separate depth-sorted list instead, since blending order is
	 * depth-first, not PSO-first.
	 */
	bool
	IsTransparentPso(uint32_t pso) noexcept;
}
