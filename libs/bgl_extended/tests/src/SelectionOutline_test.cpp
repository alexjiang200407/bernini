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

	constexpr float
	EdgeColumn(uint32_t size)
	{
		const float half = static_cast<float>(size) / 2.0f;
		return half - half * (1.0f / (c_CameraDist - 1.0f)) / c_TanHalfFov;
	}

	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = false;
		return opts;
	}

	/**
	 * The width in pixels of the outline band outside the cube's left silhouette edge.
	 *
	 * The outline colour is the only thing in the frame with r != b -- the background is black and
	 * the unlit cube grey -- and the mask is unfiltered, so over a box straddling the edge the mean
	 * of (r - b) is the fraction of columns that are outline. Times the box width, that is the
	 * band. An un-outlined frame scores ~0.
	 */
	float
	OutlineBandWidth(const std::string& path, uint32_t size)
	{
		// Wide enough outside the edge to hold the widest band the shader will draw; only just
		// inside it, because the cube's own r - b is not exactly zero and every column of it the
		// box covers is error.
		constexpr int c_ProbeOutside = 24;
		constexpr int c_ProbeInside  = 8;
		constexpr int c_ProbeWidth   = c_ProbeOutside + c_ProbeInside;
		constexpr int c_ProbeRows    = 16;

		const int x = static_cast<int>(EdgeColumn(size)) - c_ProbeOutside;
		const int y = static_cast<int>(size) / 2 - c_ProbeRows / 2;

		const auto probe = bgl::test::MeanColor(path, x, y, c_ProbeWidth, c_ProbeRows);

		return (probe.r - probe.b) * static_cast<float>(c_ProbeWidth);
	}

	bgl::test::Rgba
	CenterProbe(const std::string& path, uint32_t size)
	{
		const int origin = static_cast<int>(size) / 2 - 8;
		return bgl::test::MeanColor(path, origin, origin, 16, 16);
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

	const auto centerOff = CenterProbe(offPath, c_Size);
	INFO("cube grey: " << centerOff.r << " " << centerOff.g << " " << centerOff.b);

	// The cube rendered -- without this the edge probes assert about an empty image.
	REQUIRE(centerOff.Luma() > 0.05f);

	CHECK(OutlineBandWidth(offPath, c_Size) < 0.5f);

	view->SetSubmeshSelected(instance, 0, true);
	capture(onPath);

	// 256 lines is an eighth of the 2160 the width is authored at, so the band is the one-texel
	// floor rather than the scaled width.
	const float bandOn = OutlineBandWidth(onPath, c_Size);
	INFO("outline band: " << bandOn << " px");
	CHECK(bandOn > 0.5f);

	// The outline contours the silhouette; the surface itself is left untinted.
	const auto centerOn = CenterProbe(onPath, c_Size);
	CHECK(std::abs(centerOn.r - centerOn.b) < 0.05f);
	CHECK(std::abs(centerOn.Luma() - centerOff.Luma()) < 0.05f);

	// The target toggle is presentation only: the marks survive it, so re-enabling restores the
	// outline without the selection being re-applied.
	const std::string disabledPath  = "assets/golden/selection_outline_disabled.got.png";
	const std::string reenabledPath = "assets/golden/selection_outline_reenabled.got.png";

	target->SetOutlineEnabled(false);
	capture(disabledPath);

	CHECK(OutlineBandWidth(disabledPath, c_Size) < 0.5f);
	CHECK(view->IsSubmeshSelected(instance, 0));

	target->SetOutlineEnabled(true);
	capture(reenabledPath);

	CHECK(OutlineBandWidth(reenabledPath, c_Size) > 0.5f);

	view->ClearSelection();
	capture(clearPath);

	CHECK(OutlineBandWidth(clearPath, c_Size) < 0.5f);

	std::remove(offPath.c_str());
	std::remove(onPath.c_str());
	std::remove(clearPath.c_str());
	std::remove(disabledPath.c_str());
	std::remove(reenabledPath.c_str());
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

	const float band = OutlineBandWidth(grownPath, c_Size);
	INFO("outline band with grown selection: " << band << " px");
	CHECK(band > 0.5f);

	std::remove(grownPath.c_str());
}

TEST_CASE("The outline keeps its share of the frame as the resolution drops", "[selection][render]")
{
	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 40000;
	sceneDesc.initialIndices              = 1000;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	auto geom = scene->AddCubeGeom(bgl::MaterialHandle());
	REQUIRE(geom.IsValid());

	auto instance = view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));
	REQUIRE(instance.IsValid());
	view->SetSubmeshSelected(instance, 0, true);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, c_CameraDist),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(60.0f), 1.0f, 0.5f, 500.0f);

	// 4 px at 2160 lines, scaled by the target's height. Halving the resolution halves the band, so
	// the outline covers the same share of the frame -- which is what a render scale does to a
	// viewport before the compositor stretches it back to the window.
	struct Case
	{
		uint32_t size;
		float    expectedPx;
	};

	const std::array<Case, 3> cases{ { { 2160, 4.0f }, { 1080, 2.0f }, { 540, 1.0f } } };

	for (const auto& [size, expectedPx] : cases)
	{
		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = static_cast<int>(size);
		targetDesc.height   = static_cast<int>(size);
		targetDesc.headless = true;

		auto target = gfx->CreateRenderTarget(targetDesc);
		REQUIRE(target != nullptr);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(size), static_cast<float>(size));

		const std::string path =
			"assets/golden/selection_outline_" + std::to_string(size) + ".got.png";

		gfx->DrawFrame(target, job);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);

		const float band = OutlineBandWidth(path, size);
		INFO("at " << size << " lines: band " << band << " px, expected " << expectedPx);
		CHECK(std::abs(band - expectedPx) < 0.75f);

		std::remove(path.c_str());
	}
}
