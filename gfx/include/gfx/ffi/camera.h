#ifndef GFX_CAMERA_H
#define GFX_CAMERA_H
#include <gfx/ffi/common.h>
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

	typedef struct GfxCameraTransformOptions
	{
		GfxVec3 position;
		GfxVec3 forward;
	} GfxCameraTransformOptions;

	typedef struct GfxCameraProjectionOptions
	{
		float fovYDeg;
		float aspectRatio;
		float nearZ;
		float farZ;
	} GfxCameraProjectionOptions;

	typedef struct GfxCameraDesc
	{
		GfxCameraTransformOptions  transform;
		GfxCameraProjectionOptions projection;
	} GfxCameraDesc;

	/// <summary>
	/// Create camera object.
	/// </summary>
	/// <param name="desc"></param>
	/// <param name="out"></param>
	/// <returns></returns>
	GFX_API GfxResult
	createCamera(GfxCameraDesc desc, GfxCamera* out);

	/// <summary>
	/// Move the camera in the current foward direction by delta units.
	/// </summary>
	/// <param name="camera">Camera handle</param>
	/// <param name="delta">Units to move</param>
	/// <returns>Error enumeration</returns>
	GFX_API GfxResult
	cameraMoveAlongView(GfxCamera camera, float delta);

	/// <summary>
	/// Move the camera in the current right direction by delta units.
	/// </summary>
	/// <param name="camera">Camera handle</param>
	/// <param name="delta">Units to move</param>
	/// <returns>Error enumeration</returns>
	GFX_API GfxResult
	cameraMoveAlongRight(GfxCamera camera, float delta);

	/// <summary>
	/// Rotate the camera by deltaYaw and deltaPitch in degrees. Yaw is applied first, then pitch.
	/// </summary>
	/// <param name="camera"></param>
	/// <param name="deltaYaw"></param>
	/// <param name="deltaPitch"></param>
	/// <returns></returns>
	GFX_API GfxResult
	cameraRotateYawPitch(GfxCamera camera, float deltaYaw, float deltaPitch);

#ifdef __cplusplus
}
#endif

#endif  // !GFX_CAMERA_H
