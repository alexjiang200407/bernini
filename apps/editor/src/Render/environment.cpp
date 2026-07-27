#include "Render/environment.h"

#include <QLoggingCategory>

#include <assetlib/benv_io.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/SkyboxDesc.h>

namespace editor
{
	void
	ApplyEnvironment(
		bgl::IScene*         scene,
		bgl::ISceneView*     view,
		const std::string&   benvPath,
		std::optional<float> exposureOverride,
		const char*          who)
	{
		if (benvPath.empty())
			return;

		auto maps = assetlib::EnvironmentMaps();
		try
		{
			maps = assetlib::loadBenv(benvPath);
		}
		catch (const std::exception& e)
		{
			qWarning("%s: cannot load environment '%s': %s", who, benvPath.c_str(), e.what());
			return;
		}

		// The .benv's own exposure is the value derived from these maps, so it is the right default;
		// config only overrules it deliberately.
		view->SetExposure(exposureOverride.value_or(maps.exposure));

		try
		{
			const auto irradiance = scene->AddTextureAsset(std::move(maps.irradiance));
			const auto prefilter  = scene->AddTextureAsset(std::move(maps.prefilter));

			// Both or neither: they are the diffuse and specular convolutions of one radiance, so a
			// view holding one of them would light the scene from half an environment.
			if (irradiance.textureSlot && prefilter.textureSlot)
				view->SetEnvironmentMap({ irradiance, prefilter });
		}
		catch (const std::exception& e)
		{
			qWarning("%s: SetEnvironmentMap failed: %s", who, e.what());
		}

		try
		{
			if (const auto skybox = scene->AddTextureAsset(std::move(maps.skybox));
			    skybox.textureSlot)
				view->SetSkyBox({ skybox });
		}
		catch (const std::exception& e)
		{
			qWarning("%s: SetSkyBox failed: %s", who, e.what());
		}
	}
}
