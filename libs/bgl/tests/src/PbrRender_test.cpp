#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include <DirectXTex.h>
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	struct DiagAssertionHandler : public bgl::IGpuAssertionHandler
	{
		int                   calls = 0;
		std::vector<uint32_t> errcodes;

		void
		OnGpuAssertion(const bgl::GpuAssertionReport& report) noexcept override
		{
			++calls;
			errcodes.assign(report.errcodes, report.errcodes + report.errcodeCount);
		}
	};
}

// PBR with Image Based Lighting
TEST_CASE("PBR instances render headlessly", "[pbr][ibl][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;
	opts.logLevel         = bgl::GraphicsOptions::LogLevel::kTrace;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	DiagAssertionHandler handler;
	gfx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 400;
	targetDesc.height   = 300;
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
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
		{ scene->AddTextureAsset(assetlib::loadDDS("assets/iem.dds")),
	      scene->AddTextureAsset(assetlib::loadDDS("assets/pmrem.dds")),
	      scene->AddTextureAsset(assetlib::loadDDS("assets/brdf_lut.dds")) });

	auto metalMat = scene->CreatePbrMaterial(
		{ .baseColorFactor = glm::vec4(1.0f), .metallicFactor = .6f, .roughnessFactor = .3f });

	auto sphere = scene->AddSphereGeom(32, 32, 5.0f, metalMat);

	auto transform = glm::mat4(1.0f);
	view->CreateStaticMeshInstance(sphere, transform);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 20.0f),
			glm::vec3(0.0f, 0.0f, 19.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 400.0f / 300.0f, 0.5f, 500.0f);

	auto context     = bgl::RenderContext();
	context.view     = view;
	context.camera   = camera;
	context.viewport = bgl::Viewport(400.0f, 300.0f);

	for (int i = 0; i < 6; ++i)
	{
		gfx->DrawFrame(target, context);
	}

	gfx->ScreenshotRaw(target, "assets/golden/pbr_ibl.got.dds");

	CHECK(
		bgl::test::MatchesGoldenDDS(
			"assets/golden/pbr_ibl.exp.dds",
			"assets/golden/pbr_ibl.got.dds"));

	std::string ecStr;
	for (auto ec : handler.errcodes)
	{
		ecStr += std::to_string(ec) + " ";
	}
	INFO("GPU assertion calls: " << handler.calls << " errcodes: [" << ecStr << "]");
}
