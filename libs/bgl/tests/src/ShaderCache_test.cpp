#include "util/GoldenImage.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	namespace fs = std::filesystem;

	// Renders the same PBR sphere the golden test uses, through a Graphics configured
	// with the given shader cache directory, and captures it to gotPath.
	void
	RenderPbrSphere(const std::string& shaderCacheDir, const std::string& gotPath)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = true;
		opts.shaderCacheDir   = shaderCacheDir;

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto ctx = gfx->CreateRenderContext();

		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = 400;
		targetDesc.height   = 300;
		targetDesc.headless = true;
		auto target         = ctx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxGeom                 = 8;
		sceneDesc.maxMeshlets             = 512;
		sceneDesc.maxSubmeshes            = 8;
		sceneDesc.maxVertexBufferByteSize = 800000;
		sceneDesc.maxIndices              = 20000;
		sceneDesc.maxPbrMaterials         = 8;

		auto scene = gfx->CreateScene(sceneDesc);
		auto view  = gfx->CreateSceneView(scene, 8);

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadKTX2("assets/iem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/pmrem.ktx2")),
		      scene->AddTextureAsset(assetlib::loadKTX2("assets/brdf_lut.ktx2")) });

		auto metalMat = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = .6f, .roughnessFactor = .3f });

		auto sphere    = scene->AddSphereGeom(32, 32, 5.0f, metalMat);
		auto transform = glm::mat4(1.0f);
		view->CreateStaticMeshInstance(sphere, transform);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(400.0f, 300.0f);

		for (int i = 0; i < 6; ++i) ctx->DrawFrame(target, job);

		ctx->ScreenshotPng(target, gotPath);
	}

	// The program cache: one .bsc per compiled shader program (DXIL + reflection).
	std::vector<fs::path>
	ProgramCacheFiles(const fs::path& dir)
	{
		std::vector<fs::path> files;
		for (const auto& entry : fs::directory_iterator(dir))
		{
			if (entry.path().extension() == ".bsc")
				files.push_back(entry.path());
		}
		std::sort(files.begin(), files.end());
		return files;
	}
}

// The persistent shader cache must (1) populate itself on a cold run, (2) be reused on
// a warm run WITHOUT recompiling, and (3) never change what is rendered. The "without
// recompiling" claim is the load-bearing one, so it is proven by a negative: on the
// warm run no cache file is rewritten (an unchanged write-time means Store was never
// called, i.e. every pipeline hit the cache instead of going through slang).
TEST_CASE(
	"Shader cache is populated cold and reused warm without recompiling",
	"[shadercache][render]")
{
	const std::string golden   = "assets/golden/pbr_ibl.exp.png";
	const fs::path    cacheDir = fs::temp_directory_path() / "bernini_shadercache_test";

	std::error_code ec;
	fs::remove_all(cacheDir, ec);

	// Cold run: cache is empty, so every pipeline compiles from source and is stored.
	RenderPbrSphere(cacheDir.string(), "assets/golden/shadercache_cold.got.png");
	CHECK(bgl::test::MatchesGolden(golden, "assets/golden/shadercache_cold.got.png"));

	const std::vector<fs::path> coldFiles = ProgramCacheFiles(cacheDir);
	REQUIRE_FALSE(coldFiles.empty());

	// Both layers must have persisted something: DXIL/reflection programs and the
	// driver pipeline library.
	CHECK(fs::exists(cacheDir / "pipelines.psolib"));

	std::vector<fs::file_time_type> coldTimes;
	for (const fs::path& file : coldFiles) coldTimes.push_back(fs::last_write_time(file));

	// Warm run: same cache directory. Output must be identical, and no PROGRAM cache
	// file may be rewritten -- proof no shader went through slang again. (The pipeline
	// library may legitimately be rewritten: whether the driver round-trips a given PSO
	// through ID3D12PipelineLibrary is its own business, so it is not asserted here.)
	RenderPbrSphere(cacheDir.string(), "assets/golden/shadercache_warm.got.png");
	CHECK(bgl::test::MatchesGolden(golden, "assets/golden/shadercache_warm.got.png"));

	const std::vector<fs::path> warmFiles = ProgramCacheFiles(cacheDir);
	CHECK(warmFiles == coldFiles);

	// The extra parens keep Catch from decomposing the comparison: libc++'s file_time_type has a
	// __int128 rep it cannot stringify, so it is checked as a plain bool.
	for (size_t i = 0; i < warmFiles.size() && i < coldTimes.size(); ++i)
		CHECK((fs::last_write_time(warmFiles[i]) == coldTimes[i]));

	// A corrupt entry must not be trusted: the run recovers by recompiling.
	{
		std::ofstream(coldFiles.front(), std::ios::binary | std::ios::trunc) << "garbage";
	}
	RenderPbrSphere(cacheDir.string(), "assets/golden/shadercache_corrupt.got.png");
	CHECK(bgl::test::MatchesGolden(golden, "assets/golden/shadercache_corrupt.got.png"));

	fs::remove_all(cacheDir, ec);
}
