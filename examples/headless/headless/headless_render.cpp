#include <assetlib/project_layout.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <bgl/types/SceneDesc.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <gamelib/AssetManager.h>
#include <headless/headless_render.h>
#include <iostream>
#include <string_view>
#include <utility>

namespace headless
{
	bgl::GraphicsRef
	CreateHeadlessGraphics(const std::filesystem::path& dataRoot)
	{
		auto opts           = bgl::GraphicsOptions();
		opts.logLevel       = bgl::GraphicsOptions::LogLevel::kError;
		opts.shaderCacheDir = "shadercache";
		opts.maxTextures    = 512;
		opts.maxSrvs        = 1024;
		opts.maxCbvSrvUavs  = 4096;

		const auto surfaceDir = dataRoot / assetlib::c_ShadersDirectoryName;
		if (std::filesystem::is_directory(surfaceDir))
			opts.surfaceShaderDir = surfaceDir;
		return bgl::CreateGraphics(opts);
	}

	bgl::RenderTargetRef
	CreateHeadlessTarget(
		const bgl::GraphicsRef& graphics,
		const uint32_t          width,
		const uint32_t          height,
		const bool              taa,
		const float             renderScale)
	{
		auto desc        = bgl::RenderTargetDesc();
		desc.width       = static_cast<int>(width);
		desc.height      = static_cast<int>(height);
		desc.headless    = true;
		desc.taaEnabled  = taa;
		desc.renderScale = renderScale;
		return graphics->CreateRenderTarget(desc);
	}

	bgl::SceneRef
	CreateHeadlessScene(const bgl::GraphicsRef& graphics)
	{
		auto desc                        = bgl::SceneDesc();
		desc.initialGeom                 = 128;
		desc.initialMeshlets             = 65536;
		desc.initialSubmeshes            = 512;
		desc.initialVertexBufferByteSize = 64u << 20;
		desc.initialIndices              = 4000000;
		desc.initialPbrMaterials         = 128;
		desc.initialLoosePbrMaterials    = 128;
		return graphics->CreateScene(std::move(desc));
	}

	bool
	LightView(
		const bgl::SceneViewRef& view,
		game::AssetManager&      assets,
		const std::string_view   envKey)
	{
		try
		{
			const auto env = assets.AcquireEnvironment(envKey);
			if (env.HasLighting())
			{
				view->SetEnvironmentMap({ env.irradiance, env.prefilter });
				view->SetExposure(env.exposure);
			}
			if (env.HasSky())
				view->SetSkyBox(
					bgl::SkyboxDesc{ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
			return env.HasLighting();
		}
		catch (const std::exception& e)
		{
			std::cerr << std::format("{}: {}\nRendering unlit.\n", envKey, e.what());
			return false;
		}
	}

	glm::vec3
	SunDirection(float azimuth, float elevation) noexcept
	{
		const auto toSun = glm::vec3(
			std::cos(elevation) * std::sin(azimuth),
			std::sin(elevation),
			std::cos(elevation) * std::cos(azimuth));

		return -toSun;
	}
}
