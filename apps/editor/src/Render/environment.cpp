#include "Render/environment.h"
#include <assetlib/envmap.h>

#include <QLoggingCategory>

#include <assetlib/AssetStore.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/RimLightDesc.h>
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
		std::optional<uint32_t>      skyMipLevelOverride,
		const char*                  who)
	{
		auto applied = AppliedEnvironment();

		if (benvPath.empty())
			return applied;

		auto env = assetlib::ResolvedEnvironment();
		try
		{
			env =
				assetlib::AssetStore(dataRoot).ResolveEnvironment(std::filesystem::path(benvPath));
		}
		catch (const std::exception& e)
		{
			qWarning("%s: cannot load environment '%s': %s", who, benvPath.c_str(), e.what());
			return applied;
		}

		// The lighting's own exposure is the value derived from these maps, so it is the right
		// default; config only overrules it deliberately.
		view->SetExposure(exposureOverride.value_or(env.maps.exposure));

		// Before the maps, and outside their try: a rim takes no texture slot, so it cannot fail
		// the way a map can, and an environment that resolves to no maps at all still has one.
		view->SetRimLight(
			{ .tint = env.rim.tint, .intensity = env.rim.intensity, .power = env.rim.power });

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
				view->SetSkyBox(
					{ skybox,
				      skyMipLevelOverride.value_or(env.skyMipLevel),
				      1.0f,
				      env.skyRotationY });
				applied.skybox = skybox;
			}
		}
		catch (const std::exception& e)
		{
			qWarning("%s: SetSkyBox failed: %s", who, e.what());
		}

		return applied;
	}

	AppliedEnvironment
	ReplaceEnvironment(
		bgl::IScene*              scene,
		const AppliedEnvironment& previous,
		const AppliedEnvironment& applied)
	{
		const auto replace = [scene](bgl::TextureAssetHandle prev, bgl::TextureAssetHandle next) {
			if (!next.textureSlot)
				return prev;
			if (prev.textureSlot && prev.textureSlot != next.textureSlot)
				scene->DeleteTextureAsset(prev);
			return next;
		};

		auto bound       = AppliedEnvironment();
		bound.irradiance = replace(previous.irradiance, applied.irradiance);
		bound.prefilter  = replace(previous.prefilter, applied.prefilter);
		bound.skybox     = replace(previous.skybox, applied.skybox);
		return bound;
	}

	QStringList
	GetHeldOpenEnvironment(const EnvironmentBinding& binding)
	{
		if (binding.boundPath.empty())
			return {};

		return { QString::fromStdString(binding.boundPath) };
	}

	std::optional<std::string>
	GetEnvironmentToRestore(const EnvironmentBinding& binding)
	{
		if (binding.configured.environmentMap.empty() ||
		    binding.boundPath == binding.configured.environmentMap)
			return std::nullopt;

		return binding.configured.environmentMap;
	}

	void
	BindEnvironment(
		bgl::IScene*                 scene,
		bgl::ISceneView*             view,
		EnvironmentBinding&          binding,
		const std::string&           benvPath,
		const std::filesystem::path& dataRoot,
		const char*                  who)
	{
		const AppliedEnvironment applied = ApplyEnvironment(
			scene,
			view,
			benvPath,
			dataRoot,
			binding.configured.exposureOverride,
			binding.configured.skyMipLevelOverride,
			who);

		// After the new one is bound, never before: releasing first would leave the view naming a
		// slot that had been handed back.
		binding.bound     = ReplaceEnvironment(scene, binding.bound, applied);
		binding.boundPath = benvPath;
	}
}
