#include "gfx/GraphicsBase.h"
#include <DirectXTex.h>
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderContext.h>

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

// Reproduces the bgl_base scene (two PBR instances sharing one PSO bucket) headlessly
// under the D3D12 debug layer, so amplification/mesh-shader issues surface in the log.
TEST_CASE("PBR instances render headlessly", "[pbr][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = true;
	opts.logLevel         = bgl::GraphicsOptions::LogLevel::kTrace;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	DiagAssertionHandler handler;
	gfx->SetGpuAssertionHandler(&handler);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
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

	scene->SetEnvironmentMap(
		{ assetlib::loadDDS("assets/iem.dds"),
		  assetlib::loadDDS("assets/pmrem.dds"),
		  assetlib::loadDDS("assets/brdf_lut.dds") });

	auto metalMat   = scene->CreatePbrMaterial({ .metallicFactor = 1.0f, .roughnessFactor = 0.2f });
	auto plasticMat = scene->CreatePbrMaterial({ .metallicFactor = 0.0f, .roughnessFactor = 0.4f });

	auto cube   = scene->AddCubeGeom(metalMat);
	auto sphere = scene->AddSphereGeom(32, 32, 1.0f, plasticMat);

	auto transform = glm::mat4(1.0f);
	view->CreateStaticMeshInstance(cube, metalMat, transform);
	transform[3][0] = -5.0f;
	view->CreateStaticMeshInstance(sphere, plasticMat, transform);

	auto camera = bgl::Camera();
	camera
		.LookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f, 0.0f, 19.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto context     = bgl::RenderContext();
	context.view     = view;
	context.camera   = camera;
	context.viewport = bgl::Viewport(64.0f, 64.0f);

	for (int i = 0; i < 6; ++i)
	{
		gfx->DrawFrame(target, context);
	}

	std::string ecStr;
	for (auto ec : handler.errcodes)
	{
		ecStr += std::to_string(ec) + " ";
	}
	INFO("GPU assertion calls: " << handler.calls << " errcodes: [" << ecStr << "]");

	// Prove the geometry actually rasterized: read back the render target and require
	// that some pixels differ from the top-left (background) pixel. If the amp shader
	// mis-reads its cbuffer, the mesh shader never runs and the whole frame is uniform.
	gfx->ScreenshotRaw(target, "pbr_headless.dds");

	DirectX::TexMetadata  meta{};
	DirectX::ScratchImage img;
	REQUIRE(SUCCEEDED(DirectX::LoadFromDDSFile(L"pbr_headless.dds", DirectX::DDS_FLAGS_NONE, &meta, img)));

	const DirectX::Image* image = img.GetImage(0, 0, 0);
	REQUIRE(image != nullptr);

	const auto*  pixels    = image->pixels;
	const size_t rowPitch  = image->rowPitch;
	uint32_t     firstTexel = *reinterpret_cast<const uint32_t*>(pixels);
	bool         anyDiffer  = false;
	for (size_t y = 0; y < image->height && !anyDiffer; ++y)
	{
		const auto* row = reinterpret_cast<const uint32_t*>(pixels + y * rowPitch);
		for (size_t x = 0; x < image->width; ++x)
		{
			if (row[x] != firstTexel)
			{
				anyDiffer = true;
				break;
			}
		}
	}

	CHECK(anyDiffer);  // geometry rendered -> not a uniform frame
}
