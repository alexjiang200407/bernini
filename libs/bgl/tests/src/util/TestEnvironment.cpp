#include "util/TestEnvironment.h"

#include <assetlib/benv_io.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>

namespace bgl::test
{
	void
	ApplyEnvironment(bgl::IScene* scene, bgl::ISceneView* view)
	{
		auto env = assetlib::loadBenv("assets/forest.benv");

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(std::move(env.irradiance)),
		      scene->AddTextureAsset(std::move(env.prefilter)),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

		view->SetExposure(env.exposure);
	}

	bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene)
	{
		return scene->AddTextureAsset(assetlib::loadBenv("assets/forest.benv").skybox);
	}
}
