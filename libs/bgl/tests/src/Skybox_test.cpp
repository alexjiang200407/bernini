#include "gfx/GraphicsBase.h"
#include "util/TestOptions.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>

// SkyboxPass resolves its constant buffer by the name Slang reflection reports for the
// shader global, and gfatal()s when the lookup misses. Renaming the ConstantBuffer in
// Skybox.slang without updating the lookup therefore terminates the process the first
// time a skybox is drawn -- and no other test binds one, so nothing catches it. This
// draws a frame with a skybox bound.
TEST_CASE("Skybox renders headlessly", "[skybox][render]")
{
	constexpr uint32_t kWidth  = 200;
	constexpr uint32_t kHeight = 150;

	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(kWidth);
	targetDesc.height   = static_cast<int>(kHeight);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.maxGeom                 = 4;
	sceneDesc.maxMeshlets             = 256;
	sceneDesc.maxSubmeshes            = 4;
	sceneDesc.maxVertexBufferByteSize = 400000;
	sceneDesc.maxIndices              = 10000;
	sceneDesc.maxPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	auto cubeTex = scene->AddTextureAsset(assetlib::loadKTX2("assets/skybox.ktx2"));
	REQUIRE(cubeTex.textureSlot);

	view->SetSkyBox(bgl::SkyboxDesc{ cubeTex });

	// A geom keeps the forward pass on a realistic path; the skybox draws behind it.
	auto geom = scene->AddCubeGeom();
	(void)view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 5.0f),
			glm::vec3(0.0f, 0.0f, 4.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(kWidth) / static_cast<float>(kHeight),
			0.5f,
			500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(kWidth), static_cast<float>(kHeight));

	REQUIRE_NOTHROW(gfx->DrawFrame(target, job));
}
