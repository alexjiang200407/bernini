#pragma once
#include "idl/MeshInstance.h"
#include "idl/PsoType.h"
#include "types/Format.h"
#include "types/FormatInfo.h"
#include <bgl/GeomType.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MaterialType.h>
#include <bgl/glm.h>

namespace bgl
{
	FormatInfo
	GetFormatInfo(Format format);

	idl::PsoType
	GetPsoFromGeomAndMaterial(GeomType geom, MaterialType material, LayerType layer);

	/**
	 * The PSO bucket for `SubmeshInstance::pso`. An invalid handle resolves to the unlit `kNull`
	 * material, so a submesh that names no material renders flat rather than failing to load.
	 */
	uint32_t
	SubmeshPso(GeomType geomType, MaterialHandle material);

	/**
	 * Whether `geomType` can be drawn with `material`, which is what every door binding one to
	 * animated geometry checks. Static geometry takes anything; the animated tiers take every layer
	 * of a `kPBR` material and no other material type, having neither an unlit nor a loose variant.
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
}
