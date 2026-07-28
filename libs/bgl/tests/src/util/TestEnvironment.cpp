#include "util/TestEnvironment.h"

#include <assetlib/benv_io.h>
#include <assetlib_structs/ImageData.h>

namespace bgl::test
{
	void
	ApplyEnvironment(bgl::IScene* scene, bgl::ISceneView* view)
	{
		auto env = assetlib::loadBenv("assets/forest.benv");

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(std::move(env.irradiance)),
		      scene->AddTextureAsset(std::move(env.prefilter)) });

		view->SetExposure(env.exposure);
	}

	bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene)
	{
		return scene->AddTextureAsset(assetlib::loadBenv("assets/forest.benv").skybox);
	}
}
