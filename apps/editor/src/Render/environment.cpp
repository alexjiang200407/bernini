#include "Render/environment.h"

#include <QLoggingCategory>

#include <assetlib/env_resolve.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/SkyboxDesc.h>

namespace editor
{
	AppliedEnvironment
	ApplyEnvironment(
		bgl::IScene*                 scene,
		bgl::ISceneView*             view,
		const std::string&           benvPath,
		const std::filesystem::path& dataRoot,
		std::optional<float>         exposureOverride,
		const char*                  who)
	{
		auto applied = AppliedEnvironment();

		if (benvPath.empty())
			return applied;

		auto env = assetlib::ResolvedEnvironment();
		try
		{
			env = assetlib::resolveEnvironment(std::filesystem::path(benvPath), dataRoot);
		}
		catch (const std::exception& e)
		{
			qWarning("%s: cannot load environment '%s': %s", who, benvPath.c_str(), e.what());
			return applied;
		}

		// The lighting's own exposure is the value derived from these maps, so it is the right
		// default; config only overrules it deliberately.
		view->SetExposure(exposureOverride.value_or(env.maps.exposure));

		try
		{
			const auto irradiance = scene->AddTextureAsset(std::move(env.maps.irradiance));
			const auto prefilter  = scene->AddTextureAsset(std::move(env.maps.prefilter));

			// Both or neither: they are the diffuse and specular convolutions of one radiance, so a
			// view holding one of them would light the scene from half an environment.
			if (irradiance.textureSlot && prefilter.textureSlot)
			{
				view->SetEnvironmentMap({ irradiance, prefilter });
				applied.irradiance = irradiance;
				applied.prefilter  = prefilter;
			}
		}
		catch (const std::exception& e)
		{
			qWarning("%s: SetEnvironmentMap failed: %s", who, e.what());
		}

		try
		{
			if (const auto skybox = scene->AddTextureAsset(std::move(env.maps.skybox));
			    skybox.textureSlot)
			{
				view->SetSkyBox({ skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
				applied.skybox = skybox;
			}
		}
		catch (const std::exception& e)
		{
			qWarning("%s: SetSkyBox failed: %s", who, e.what());
		}

		return applied;
	}
}
