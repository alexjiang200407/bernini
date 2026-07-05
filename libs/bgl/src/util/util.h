#pragma once
#include "types/Format.h"
#include "types/FormatInfo.h"
#include <bgl/GeomType.h>
#include <bgl/MaterialType.h>
#include <bgl/PsoType.h>

namespace bgl
{
	FormatInfo
	GetFormatInfo(Format format);

	PsoType
	GetPsoFromGeomAndMaterial(GeomType geom, MaterialType material);
}
