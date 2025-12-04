#include "GfxBase.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"
#include <gfx/camera.h>

namespace gfx
{
	struct CameraData
	{
		glm::mat4 viewMatrix;
		glm::mat4 projMatrix;
	};

	class Camera : public GfxBase
	{
	public:
		explicit Camera(IGraphics& graphics, const CameraOptions& options)
		{
			const float fovYRadians = glm::radians(options.fovYDegrees);
			data.projMatrix         = gfx::math::toShaderLayout(
                glm::perspective(fovYRadians, options.aspectRatio, options.nearZ, options.farZ));

			glm::vec3 camPos{ options.position[0], options.position[1], options.position[2] };
			glm::vec3 camTarget{ 0.0f, 0.0f, 0.0f };
			glm::vec3 camUp{ 0.0f, 1.0f, 0.0f };

			data.viewMatrix = gfx::math::toShaderLayout(glm::lookAt(camPos, camTarget, camUp));

			cbuf =
				graphics.GetDevice()->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(
					sizeof(CameraData),
					"CameraConstantBuffer",
					16));
		}

		nvrhi::BufferHandle
		Update(nvrhi::CommandListHandle cmdList)
		{
			cmdList->writeBuffer(cbuf, &data, sizeof(data));
			return cbuf;
		}

	private:
		CameraData          data;
		nvrhi::BufferHandle cbuf;
	};
}

GfxResult
createCamera(Graphics gfx, CameraOptions options, Camera* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		gfx::ffi::validatePtr(out, "out");

		auto& gfx_   = gfx::ffi::gfxObjCast<gfx::IGraphics>(gfx);
		out->data    = new gfx::Camera{ gfx_, options };
		out->destroy = gfx::ffi::deleteThunk;

		return GFX_RESULT_OK;
	});
}
