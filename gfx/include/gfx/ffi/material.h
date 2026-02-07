#ifndef GFX_MATERIAL_H
#define GFX_MATERIAL_H
#include <gfx/ffi/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

	GfxResult
	createSolidColorMaterial(Gfx gfx, GfxVec3 color);

#ifdef __cplusplus
}
#endif

#endif
