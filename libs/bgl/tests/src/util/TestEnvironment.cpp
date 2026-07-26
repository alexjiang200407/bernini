#include "util/TestEnvironment.h"

#include <assetlib/benv_io.h>
#include <assetlib/image_io.h>

namespace bgl_test
{
	bgl::EnvironmentMapDesc
	LoadEnvironment(bgl::IScene* scene)
	{
		auto env = assetlib::loadBenv("assets/forest.benv");
		return { scene->AddTextureAsset(std::move(env.irradiance)),
			     scene->AddTextureAsset(std::move(env.prefilter)),
			     scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) };
	}

	bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene)
	{
		return scene->AddTextureAsset(assetlib::loadBenv("assets/forest.benv").skybox);
	}
}
