#ifndef GFX_CAMERA_H
#define GFX_CAMERA_H
#include <gfx/common.h>
#include <stdint.h>

//       _____
//      / ____|
//     | |     __ _ _ __ ___   ___ _ __ __ _
//     | |    / _` | '_ ` _ \ / _ \ '__/ _` |
//     | |___| (_| | | | | | |  __/ | | (_| |
//      \_____\__,_|_| |_| |_|\___|_|  \__,_|
//
//

#ifdef __cplusplus
extern "C"
{
#endif

	typedef enum CameraProjectionType
	{
		CAMERA_PERSPECTIVE = 0,
		CAMERA_ORTHOGRAPHIC
	} CameraProjectionType;

	typedef struct CameraOptions
	{
		float position[3];
		float fovYDegrees;
		float aspectRatio;
		float nearZ;
		float farZ;
	} CameraOptions;

	/// <summary>
	///
	/// </summary>
	/// <param name="options"></param>
	/// <param name="out"></param>
	/// <returns></returns>
	GFX_API GfxResult
	createCamera(Graphics gfx, CameraOptions options, Camera* out);

#ifdef __cplusplus
}
#endif

#endif  // !GFX_CAMERA_H
