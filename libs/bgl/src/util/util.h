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
	GetPsoFromGeomAndMaterial(
		GeomType     geom,
		MaterialType material,
		LayerType    layer,
		bool         occlude = false);

	/**
	 * The PSO bucket for `SubmeshInstance::pso`. An invalid handle resolves to the unlit `kNull`
	 * material, so a submesh that names no material renders flat rather than failing to load.
	 */
	uint32_t
	SubmeshPso(GeomType geomType, MaterialHandle material);

	/**
	 * Whether `pso` draws with alpha blending. Its instances are excluded from the PSO-bucketed
	 * counting sort and drawn from a separate depth-sorted list instead, since blending order is
	 * depth-first, not PSO-first.
	 */
	bool
	IsTransparentPso(uint32_t pso) noexcept;

	/**
	 * Whether `pso` is a transparent material that self-occludes: it draws a depth-only pre-pass
	 * first, then blends only the front layer (`depthFunc == Equal`). See `ForwardPass::Execute`.
	 */
	bool
	IsOccludeTransparentPso(uint32_t pso) noexcept;
}
