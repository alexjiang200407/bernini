#include "Render/environment.h"

#include <QLoggingCategory>

#include <assetlib/benv_io.h>
#include <assetlib/image_io.h>
#include <bgl/SkyboxDesc.h>

namespace editor
{
	void
	ApplyEnvironment(
		bgl::IScene*         scene,
		bgl::ISceneView*     view,
		const std::string&   benvPath,
		const std::string&   brdfLutPath,
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

		auto brdfLut = bgl::TextureAssetHandle();
		if (!brdfLutPath.empty())
		{
			try
			{
				brdfLut = scene->AddTextureAsset(assetlib::loadKTX2(brdfLutPath));
			}
			catch (const std::exception& e)
			{
				qWarning("%s: cannot load '%s': %s", who, brdfLutPath.c_str(), e.what());
			}
		}

		try
		{
			const auto irradiance = scene->AddTextureAsset(std::move(maps.irradiance));
			const auto prefilter  = scene->AddTextureAsset(std::move(maps.prefilter));

			// All three or none: the split-sum specular is the product of the prefilter and the LUT,
			// so a missing LUT would leave the lobe unnormalised rather than merely dimmer.
			if (irradiance.textureSlot && prefilter.textureSlot && brdfLut.textureSlot)
				view->SetEnvironmentMap({ irradiance, prefilter, brdfLut });
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
