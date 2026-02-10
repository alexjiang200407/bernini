#include "scene/Scene.h"
#include "ffi/util.h"
#include "graphics/Graphics.h"

GfxResult
createScene(Gfx gfx, GfxScene* outScene)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& gfx_ = gfx::ffi::gfxObjCast<gfx::IGraphics>(gfx);

		outScene->ptr     = new gfx::Scene{ gfx_.GetDevice() };
		outScene->destroy = [](GfxObj self) {
			auto* scene = static_cast<gfx::Scene*>(self.ptr);
			if (scene)
				delete scene;
		};

		return GFX_RESULT_OK;
	});
}

GfxResult
createCube(GfxScene scene, GfxMat4 modelTransform, GfxMesh* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		*out = scene_.CreateCube(glm::make_mat4(modelTransform));

		return GFX_RESULT_OK;
	});
}

GfxResult
createSphere(GfxScene scene, GfxMat4 modelTransform, GfxMesh* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		*out = scene_.CreateSphere(glm::make_mat4(modelTransform));

		return GFX_RESULT_OK;
	});
}

GfxResult
destroyMesh(GfxScene scene, GfxMesh mesh)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		scene_.RemoveInstance(mesh);
		return GFX_RESULT_OK;
	});
}

namespace gfx
{
	DrawInstance::ID
	Scene::CreateCube(glm::mat4 modelTransform)
	{
		auto instance           = StaticMeshInstance{};
		instance.infoID         = m_cubeInfoID;
		instance.modelTransform = modelTransform;
		return m_data.AddStaticMeshInstance(std::move(instance));
	}

	DrawInstance::ID
	Scene::CreateSphere(glm::mat4 modelTransform)
	{
		auto instance           = StaticMeshInstance{};
		instance.infoID         = m_sphereInfoID;
		instance.modelTransform = modelTransform;
		return m_data.AddStaticMeshInstance(std::move(instance));
	}
}
