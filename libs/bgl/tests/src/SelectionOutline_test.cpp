#include "util/GoldenImage.h"
#include "util/TestOptions.h"
#include <bgl/IGraphics.h>

// The selection outline end to end: marking a submesh selected draws the mask and the
// post-process contours it; clearing the selection restores the plain image. Regions of one
// frame are compared against each other, so there is no golden PNG to regenerate.

namespace
{
	constexpr uint32_t c_Size = 256;

	// Camera at z = c_CameraDist looking at a unit-half-extent cube: the silhouette is the front
	// face, so the left edge lands at a computable column and the outline band sits just outside
	// it. fov 60 deg vertical, tan(30 deg) below.
	constexpr float c_CameraDist = 4.0f;
	constexpr float c_TanHalfFov = 0.57735f;
	constexpr float c_EdgeColumn =
		(c_Size / 2.0f) - (c_Size / 2.0f) * (1.0f / (c_CameraDist - 1.0f)) / c_TanHalfFov;

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	// A 5-wide strip straddling the outline band outside the cube's left edge. The outline is
	// pure red-vs-blue contrast; the background is black and the cube is grey, so both score ~0.
	bgl::test::Rgba
	EdgeProbe(const std::string& path)
	{
		const int x = static_cast<int>(c_EdgeColumn) - 4;
		return bgl::test::MeanColor(path, x, 120, 5, 16);
	}

	bgl::test::Rgba
	CenterProbe(const std::string& path)
	{
		return bgl::test::MeanColor(path, 120, 120, 16, 16);
	}
}

TEST_CASE("Selection outline contours the selected instance", "[selection][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Size);
	targetDesc.height   = static_cast<int>(c_Size);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 40000;
	sceneDesc.initialIndices              = 1000;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	// No material: the kNull bucket shades the cube a uniform grey, which is all the probes need.
	auto geom = scene->AddCubeGeom(bgl::MaterialHandle());
	REQUIRE(geom.IsValid());

	auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(instance.IsValid());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, c_CameraDist),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

	const auto capture = [&](const std::string& path) {
		// Two frames: the first uploads and presents, the screenshot reads the last presented.
		gfx->DrawFrame(target, job);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	const std::string offPath   = "assets/golden/selection_outline_off.got.png";
	const std::string onPath    = "assets/golden/selection_outline_on.got.png";
	const std::string clearPath = "assets/golden/selection_outline_cleared.got.png";

	capture(offPath);

	const auto centerOff = CenterProbe(offPath);
	INFO("cube grey: " << centerOff.r << " " << centerOff.g << " " << centerOff.b);

	// The cube rendered -- without this the edge probes assert about an empty image.
	REQUIRE(centerOff.Luma() > 0.05f);

	const auto edgeOff = EdgeProbe(offPath);
	CHECK(edgeOff.r - edgeOff.b < 0.05f);

	view->SetSubmeshSelected(instance, 0, true);
	capture(onPath);

	// The band is at least two orange columns of the five sampled, so the contrast is strong
	// even with a pixel of rasterization slack on the edge.
	const auto edgeOn = EdgeProbe(onPath);
	INFO("edge with outline: " << edgeOn.r << " " << edgeOn.g << " " << edgeOn.b);
	CHECK(edgeOn.r - edgeOn.b > 0.2f);

	// The outline contours the silhouette; the surface itself is left untinted.
	const auto centerOn = CenterProbe(onPath);
	CHECK(std::abs(centerOn.r - centerOn.b) < 0.05f);
	CHECK(std::abs(centerOn.Luma() - centerOff.Luma()) < 0.05f);

	view->ClearSelection();
	capture(clearPath);

	const auto edgeCleared = EdgeProbe(clearPath);
	CHECK(edgeCleared.r - edgeCleared.b < 0.05f);

	std::remove(offPath.c_str());
	std::remove(onPath.c_str());
	std::remove(clearPath.c_str());
}

TEST_CASE(
	"A selection outgrowing its list capacity survives the growth frame",
	"[selection][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Size);
	targetDesc.height   = static_cast<int>(c_Size);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 40000;
	sceneDesc.initialIndices              = 1000;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 128);

	auto geom = scene->AddCubeGeom(bgl::MaterialHandle());

	// Past the selected-list's initial GPU capacity (64), so the first selected frame grows the
	// buffer -- the imported handle must be the grown one or the mask pass dispatches over a
	// retired resource that never received the upload.
	constexpr uint32_t c_Instances = 70;

	auto instances = std::vector<bgl::MeshInstanceHandle>();
	for (uint32_t i = 0; i < c_Instances; ++i)
	{
		instances.push_back(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)));
	}

	for (const auto& instance : instances)
	{
		view->SetSubmeshSelected(instance, 0, true);
	}

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, c_CameraDist),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));

	gfx->DrawFrame(target, job);
	gfx->DrawFrame(target, job);

	const std::string grownPath = "assets/golden/selection_outline_grown.got.png";
	gfx->ScreenshotPng(target, grownPath);

	const auto edge = EdgeProbe(grownPath);
	INFO("edge with grown selection: " << edge.r << " " << edge.g << " " << edge.b);
	CHECK(edge.r - edge.b > 0.2f);

	std::remove(grownPath.c_str());
}
