#include "scene/Scene.h"
#include "GfxException.h"
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
createCubeBase(GfxScene scene, GfxStaticMesh* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		*out = scene_.CreateCubeMesh();

		return GFX_RESULT_OK;
	});
}

GfxResult
createSphereBase(GfxScene scene, GfxStaticMesh* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		*out = scene_.CreateSphereMesh();

		return GFX_RESULT_OK;
	});
}

GfxResult
createStaticMeshInstance(GfxScene scene, GfxStaticMeshOpts opts, GfxMeshInstance* out)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		gfx::ffi::validatePtr(out, "out");

		GfxMeshInstance instanceId =
			scene_.CreateStaticMeshInstance(opts.baseMesh, glm::make_mat4(opts.modelTransform));

		bool committed = false;
		auto cleanup   = gfx::ffi::make_scope_guard([&]() noexcept {
            if (!committed && instanceId != 0)
            {
                scene_.RemoveMeshInstanceNoExcept(instanceId);
            }
        });

		if (opts.material.type == GfxMaterialType_PBR)
		{
			scene_.AttachPBRMaterial(instanceId, opts.material.id);
		}
		else
		{
			return GFX_RESULT_ERROR_INVALID_ARGUMENT;
		}

		committed = true;
		*out      = instanceId;
		return GFX_RESULT_OK;
	});
}

GfxResult
destroyMeshInstance(GfxScene scene, GfxMeshInstance meshInstance)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		scene_.RemoveMeshInstance(meshInstance);
		return GFX_RESULT_OK;
	});
}

GfxResult
destroyStaticMesh(GfxScene scene, GfxStaticMesh mesh)
{
	return gfx::ffi::apiInvoke([=]() -> GfxResult {
		auto& scene_ = gfx::ffi::gfxObjCast<gfx::Scene>(scene);
		scene_.RemoveStaticMesh(mesh);
		return GFX_RESULT_OK;
	});
}

namespace gfx
{
	StaticMeshInfo::ID
	Scene::CreateCubeMesh()
	{
		return CreateCubeInfo(m_data);
	}

	StaticMeshInfo::ID
	Scene::CreateSphereMesh()
	{
		return CreateSphereInfo(m_data);
	}

	DrawInstance::ID
	Scene::CreateStaticMeshInstance(StaticMeshInfo::ID infoId, glm::mat4 modelTransform)
	{
		auto instance           = StaticMeshInstance{};
		instance.infoID         = infoId;
		instance.modelTransform = modelTransform;
		return m_data.AddStaticMeshInstance(std::move(instance));
	}

	bool
	Scene::RemoveMeshInstanceNoExcept(DrawInstance::ID id) noexcept
	{
		try
		{
			m_data.RemoveDrawInstance(id);
			return true;
		}
		catch (const std::exception& e)
		{
			logger::error("Failed to remove mesh instance with ID {}: {}", id, e.what());
			return false;
		}
		catch (...)
		{
			return false;
		}
	}
}
