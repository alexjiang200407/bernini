#include "util/TestEnvironment.h"

#include <assetlib/AssetStore.h>
#include <assetlib/env_resolve.h>
#include <assetlib_structs/ImageData.h>

namespace bgl::test
{
	void
	ApplyEnvironment(bgl::IScene* scene, bgl::ISceneView* view)
	{
		auto env =
			assetlib::AssetStore("assets").ResolveEnvironment("assets/Environments/forest.benv");

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(std::move(env.maps.irradiance)),
		      scene->AddTextureAsset(std::move(env.maps.prefilter)) });

		view->SetExposure(env.maps.exposure);
	}

	bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene)
	{
		return scene->AddTextureAsset(
			assetlib::AssetStore("assets")
				.ResolveEnvironment("assets/Environments/forest.benv")
				.maps.skybox);
	}
}
