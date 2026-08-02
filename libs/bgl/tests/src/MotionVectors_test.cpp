#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/HalfFloat.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/jitter.h"
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>
#include <catch2/catch_approx.hpp>

namespace
{
	// Square, so a sign error in one screen axis cannot hide behind the aspect ratio.
	constexpr uint32_t c_Width  = 256;
	constexpr uint32_t c_Height = 256;

	// The quad's plane. The camera looks down -Z at it from c_CameraZ, which at a 60-degree vertical
	// field of view sees about 11.5 units either side of centre -- so a quad half that wide covers
	// the middle of the frame and leaves its corners empty.
	constexpr float c_PlaneZ     = 0.0f;
	constexpr float c_CameraZ    = 20.0f;
	constexpr float c_QuadExtent = 6.0f;

	const float     c_Fov  = glm::radians(60.0f);
	constexpr float c_Near = 0.5f;
	constexpr float c_Far  = 500.0f;

	// Looks along -Z, yawed left by `yaw` about +Y.
	bgl::Camera
	cameraAt(glm::vec3 eye, float yaw = 0.0f)
	{
		const glm::vec3 forward{ -std::sin(yaw), 0.0f, -std::cos(yaw) };

		auto camera = bgl::Camera();
		camera.LookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				c_Fov,
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				c_Near,
				c_Far);
		return camera;
	}

	// The world point the centre of pixel (px, py) sees, by unprojecting that pixel's ray through
	// `camera` and intersecting it with the quad's plane. Derived independently of the shader, so
	// agreeing with it is evidence rather than tautology.
	glm::vec3
	surfacePointAt(const bgl::Camera& camera, glm::vec3 eye, uint32_t px, uint32_t py)
	{
		const float ndcX = 2.0f * ((static_cast<float>(px) + 0.5f) / c_Width) - 1.0f;
		const float ndcY = 1.0f - 2.0f * ((static_cast<float>(py) + 0.5f) / c_Height);

		const glm::vec4 unprojected =
			glm::inverse(camera.GetViewProjection()) * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		const glm::vec3 onRay = glm::vec3(unprojected) / unprojected.w;

		const float t = (c_PlaneZ - eye.z) / (onRay.z - eye.z);
		return eye + t * (onRay - eye);
	}

	// Where `worldPos` lands in screen UV under `camera` -- the same [0,1] space the shader's motion
	// vectors are expressed in.
	glm::vec2
	projectToUv(const bgl::Camera& camera, glm::vec3 worldPos)
	{
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(worldPos, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * -0.5f + 0.5f);
	}

	// Drives frames against one target and reads its velocity buffer back as floats.
	struct MotionFixture
	{
		bgl::GraphicsRef         gfx;
		bgl::RenderTargetRef     target;
		bgl::SceneRef            scene;
		bgl::SceneViewRef        view;
		bgl::ResourceManagerRef  resourceManager;
		bgl::RenderTargetBase*   targetBase = nullptr;
		bgl::CommandAllocatorRef cmdAllocator;
		bgl::CommandListRef      cmdList;
		bgl::CommandQueueRef     cmdQueue;

		explicit MotionFixture(bool taaEnabled = false)
		{
			auto opts                     = bgl::GraphicsOptions();
			opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer         = true;
			opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

			gfx = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc       = bgl::RenderTargetDesc();
			targetDesc.width      = static_cast<int>(c_Width);
			targetDesc.height     = static_cast<int>(c_Height);
			targetDesc.headless   = true;
			targetDesc.taaEnabled = taaEnabled;

			target = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			targetBase = target->As<bgl::RenderTargetBase>();
			REQUIRE(targetBase != nullptr);

			auto sceneDesc                        = bgl::SceneDesc();
			sceneDesc.initialGeom                 = 4;
			sceneDesc.initialMeshlets             = 64;
			sceneDesc.initialSubmeshes            = 4;
			sceneDesc.initialVertexBufferByteSize = 8192;
			sceneDesc.initialIndices              = 256;

			scene = gfx->CreateScene(sceneDesc);
			view  = gfx->CreateSceneView(scene, 4);

			auto* gfxBase   = gfx->As<bgl::GraphicsBase>();
			resourceManager = gfxBase->GetResourceManagerCpy();

			auto cmdListDesc = bgl::CommandListDesc();
			cmdListDesc.type = bgl::QueueType::kGraphics;

			auto* device = gfxBase->GetDevice();
			cmdAllocator = device->CreateCommandAllocator();
			cmdList      = device->CreateCommandList(cmdListDesc, cmdAllocator, resourceManager);
			cmdQueue     = device->CreateCommandQueue(bgl::QueueType::kGraphics);
		}

		// A quad facing the camera, spanning c_QuadExtent about the origin -- wide enough to cover
		// screen centre from every camera these tests use, narrow enough to leave the corners empty.
		// No material: the Null PSO shades flat white and needs no IBL, leaving the velocity output
		// as the only thing under test.
		bgl::MeshInstanceHandle
		AddQuad()
		{
			auto plane = scene->AddPlaneGeom(1, 1, c_QuadExtent * 2.0f, c_QuadExtent * 2.0f);
			return view->CreateStaticMeshInstance(
				plane,
				glm::translate(glm::mat4(1.0f), { 0, 0, c_PlaneZ }));
		}

		void
		AddSkybox()
		{
			view->SetSkyBox(bgl::SkyboxDesc{ bgl::test::LoadSkybox(scene.Get()) });
		}

		void
		RenderFrom(const bgl::Camera& camera)
		{
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

			gfx->DrawFrame(target, job);
		}

		// The velocity buffer as one float2 per pixel, row-major and tightly packed. The forward pass
		// leaves it in render-target, so it is walked back there after the copy.
		std::vector<glm::vec2>
		ReadMotionVectors()
		{
			const bgl::TextureHandle texture = targetBase->GetMotionVectorTexture();
			const auto               layout  = resourceManager->GetTextureReadbackLayout(texture);

			auto rbDesc      = bgl::ReadbackBufferDesc();
			rbDesc.byteSize  = layout.totalBytes;
			rbDesc.debugName = "Motion Vector Readback";

			auto readback = resourceManager->CreateReadbackBuffer(rbDesc);

			// This copy rides its own queue, which nothing orders against the renderer's. Without the
			// drain it reads the texture as it stood before the frame it is meant to measure.
			gfx->As<bgl::GraphicsBase>()->WaitIdle();

			cmdAllocator->ResetAllocator();
			cmdList->Open(cmdQueue, cmdAllocator);

			const auto transition = [&](bgl::BarrierLayout before, bgl::BarrierLayout after) {
				auto barrier = bgl::TextureBarrierDesc();
				barrier.AddSyncBefore(bgl::BarrierSyncFlag::kAllCommands)
					.AddAccessBefore(
						before == bgl::BarrierLayout::kRenderTarget ?
							bgl::BarrierAccessFlag::kRenderTarget :
							bgl::BarrierAccessFlag::kCopySource)
					.SetLayoutBefore(before)
					.AddSyncAfter(bgl::BarrierSyncFlag::kAllCommands)
					.AddAccessAfter(
						after == bgl::BarrierLayout::kRenderTarget ?
							bgl::BarrierAccessFlag::kRenderTarget :
							bgl::BarrierAccessFlag::kCopySource)
					.SetLayoutAfter(after);
				cmdList->Barrier(texture, barrier);
			};

			transition(bgl::BarrierLayout::kRenderTarget, bgl::BarrierLayout::kCopySource);
			cmdList->CopyTextureToReadback(readback, texture);
			transition(bgl::BarrierLayout::kCopySource, bgl::BarrierLayout::kRenderTarget);

			cmdList->Close();
			cmdQueue->WaitForFenceCPUBlocking(cmdQueue->ExecuteCommandList(cmdList));

			const auto* base = static_cast<const uint8_t*>(resourceManager->MapReadback(readback));
			REQUIRE(base != nullptr);

			auto motion = std::vector<glm::vec2>(static_cast<size_t>(c_Width) * c_Height);
			for (uint32_t y = 0; y < c_Height; ++y)
			{
				const auto* row =
					reinterpret_cast<const uint16_t*>(base + layout.offset + y * layout.rowPitch);

				for (uint32_t x = 0; x < c_Width; ++x)
				{
					motion[static_cast<size_t>(y) * c_Width + x] = glm::vec2(
						bgl::test::HalfToFloat(row[x * 2]),
						bgl::test::HalfToFloat(row[x * 2 + 1]));
				}
			}

			resourceManager->UnmapReadback(readback);
			resourceManager->DestroyReadbackBuffer(readback, false);

			return motion;
		}
	};

	glm::vec2
	centrePixel(const std::vector<glm::vec2>& motion)
	{
		return motion[static_cast<size_t>(c_Height / 2) * c_Width + (c_Width / 2)];
	}
}

// The first frame has no history to reproject through, so every pixel must read as static. If the
// previous view-projection defaulted to identity instead of the current camera, the quad would come
// out with a large bogus velocity on the very frame a consumer starts accumulating from.
TEST_CASE("The first frame a view is drawn has no motion", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();
	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));

	const auto motion = fixture.ReadMotionVectors();

	for (const glm::vec2& texel : motion)
	{
		REQUIRE(texel.x == Catch::Approx(0.0f).margin(1e-4));
		REQUIRE(texel.y == Catch::Approx(0.0f).margin(1e-4));
	}
}

// A camera that does not move leaves static geometry with no screen-space velocity. This is the
// case a stale or mis-plumbed prevViewProj breaks first: any drift between the two matrices shows
// up here as motion on a scene where nothing happened.
TEST_CASE("A still camera leaves static geometry with no motion", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();
	const auto camera = cameraAt({ 0.0f, 0.0f, c_CameraZ });

	fixture.RenderFrom(camera);
	fixture.RenderFrom(camera);

	const auto motion = fixture.ReadMotionVectors();

	for (const glm::vec2& texel : motion)
	{
		REQUIRE(texel.x == Catch::Approx(0.0f).margin(1e-4));
		REQUIRE(texel.y == Catch::Approx(0.0f).margin(1e-4));
	}
}

// The load-bearing one: the velocity written for a pixel must be the displacement from where that
// pixel's surface point sat on screen last frame. The expectation is computed on the CPU by
// unprojecting the pixel through the new camera onto the quad and re-projecting that world point
// through the old one -- so a wrong matrix, a missing perspective divide, or a flipped screen axis
// all produce a mismatch rather than a plausible-looking number.
//
// The camera moves on both screen axes at once, so a sign error in either is caught.
TEST_CASE(
	"Camera motion reprojects static geometry to its previous screen position",
	"[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();

	const glm::vec3 eyeBefore{ 0.0f, 0.0f, c_CameraZ };
	const glm::vec3 eyeAfter{ 1.0f, 0.8f, c_CameraZ };

	const bgl::Camera before = cameraAt(eyeBefore);
	const bgl::Camera after  = cameraAt(eyeAfter);

	fixture.RenderFrom(before);
	fixture.RenderFrom(after);

	const glm::vec2 measured = centrePixel(fixture.ReadMotionVectors());

	const glm::vec3 surface  = surfacePointAt(after, eyeAfter, c_Width / 2, c_Height / 2);
	const glm::vec2 expected = projectToUv(after, surface) - projectToUv(before, surface);

	INFO("measured = " << measured.x << ", " << measured.y);
	INFO("expected = " << expected.x << ", " << expected.y);

	CHECK(measured.x == Catch::Approx(expected.x).margin(1e-3));
	CHECK(measured.y == Catch::Approx(expected.y).margin(1e-3));

	// The signal has to be well clear of the tolerance, or the check above would pass on zeros.
	CHECK(std::abs(expected.x) > 1e-2f);
	CHECK(std::abs(expected.y) > 1e-2f);

	// Panning the camera up and to the right drags the surface down and to the left.
	CHECK(measured.x < 0.0f);
	CHECK(measured.y > 0.0f);
}

// The sky is infinitely far, so a camera rotation is the only thing that displaces it -- but it
// displaces all of it, which is most of a typical frame. Left at zero the sky would read as static
// through every pan.
//
// A pure yaw has a closed form: a direction that now sits at the screen's centre sat at
// tan(yaw)/tan(halfFov) in NDC before, which owes nothing to the matrices the shader uses. Turning
// left drags the sky right, and nothing moves vertically.
TEST_CASE("A camera yaw displaces the skybox horizontally", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddSkybox();

	const float yaw = glm::radians(5.0f);

	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));
	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }, yaw));

	// No quad, so the sky is what shaded every pixel including this one.
	const glm::vec2 sky = centrePixel(fixture.ReadMotionVectors());

	const float expectedX = 0.5f * std::tan(yaw) / std::tan(c_Fov * 0.5f);

	INFO("sky = " << sky.x << ", " << sky.y << "  expectedX = " << expectedX);

	// Turning left drags what was ahead of the camera off to the right.
	CHECK(sky.x == Catch::Approx(expectedX).margin(2e-3));
	CHECK(sky.x > 0.0f);

	// A yaw is horizontal; any vertical component means an axis got crossed.
	CHECK(sky.y == Catch::Approx(0.0f).margin(2e-3));
}

// Nothing drew the background, so it keeps the cleared value. A consumer reads that as "did not
// move" rather than as garbage left over from whatever the texture held before.
TEST_CASE("Pixels no geometry covered stay at zero motion", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();

	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));
	fixture.RenderFrom(cameraAt({ 3.0f, 2.0f, c_CameraZ }));

	const auto motion = fixture.ReadMotionVectors();

	// No skybox and a quad that reaches only c_QuadExtent, so the frame's corners are background.
	const glm::vec2 corner = motion[0];
	CHECK(corner.x == Catch::Approx(0.0f).margin(1e-4));
	CHECK(corner.y == Catch::Approx(0.0f).margin(1e-4));

	// ...while the centre, which the quad does cover, moved.
	CHECK(glm::length(centrePixel(motion)) > 1e-2f);
}

// A surface that was off-screen last frame still has a previous position -- it just is not one the
// history buffer holds. The velocity must carry the pixel out past the frame edge, because that is
// the only signal a consumer has that there is nothing to reproject into; a clamped or zeroed
// vector would read as "this was here all along" and blend against whatever occupied that texel.
TEST_CASE("Geometry entering the frame reprojects to outside it", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();

	// Yawed well past the 30-degree half-angle, so the quad is outside the frustum entirely.
	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }, glm::radians(60.0f)));
	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));

	const glm::vec2 motion = centrePixel(fixture.ReadMotionVectors());

	// Where the consumer would go looking for this pixel's history.
	const glm::vec2 previousUv = glm::vec2(0.5f, 0.5f) - motion;

	INFO("motion = " << motion.x << ", " << motion.y);
	INFO("previousUv = " << previousUv.x << ", " << previousUv.y);

	CHECK((previousUv.x < 0.0f || previousUv.x > 1.0f));
}

// The velocity buffer is cleared every frame, so a frame that draws nothing reports nothing. Were
// it not, the frame after geometry left would still be carrying that geometry's last velocity, and
// a consumer would reproject the background through it.
TEST_CASE("Geometry leaving the frame leaves no motion behind", "[motionvectors][render]")
{
	auto fixture = MotionFixture();
	fixture.AddQuad();

	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));
	REQUIRE(glm::length(centrePixel(fixture.ReadMotionVectors())) < 1e-4f);

	// Moving and then turning away: the frame before this one had motion everywhere the quad was.
	fixture.RenderFrom(cameraAt({ 1.0f, 0.8f, c_CameraZ }));
	REQUIRE(glm::length(centrePixel(fixture.ReadMotionVectors())) > 1e-2f);

	fixture.RenderFrom(cameraAt({ 1.0f, 0.8f, c_CameraZ }, glm::radians(60.0f)));

	for (const glm::vec2& texel : fixture.ReadMotionVectors())
	{
		REQUIRE(texel.x == Catch::Approx(0.0f).margin(1e-4));
		REQUIRE(texel.y == Catch::Approx(0.0f).margin(1e-4));
	}
}

// Same guarantee as leaving the frame, reached the other way: the instance is gone rather than
// merely unseen, so this exercises the erase path instead of the cull. A velocity that outlived
// its instance would be reprojecting pixels through geometry that no longer exists.
TEST_CASE("A deleted instance stops contributing motion", "[motionvectors][render]")
{
	auto                          fixture = MotionFixture();
	const bgl::MeshInstanceHandle quad    = fixture.AddQuad();

	fixture.RenderFrom(cameraAt({ 0.0f, 0.0f, c_CameraZ }));
	fixture.RenderFrom(cameraAt({ 1.0f, 0.8f, c_CameraZ }));
	REQUIRE(glm::length(centrePixel(fixture.ReadMotionVectors())) > 1e-2f);

	fixture.view->DeleteMeshInstance(quad);

	// The camera keeps moving, so anything still drawing would still be writing velocity.
	fixture.RenderFrom(cameraAt({ 2.0f, 1.6f, c_CameraZ }));

	for (const glm::vec2& texel : fixture.ReadMotionVectors())
	{
		REQUIRE(texel.x == Catch::Approx(0.0f).margin(1e-4));
		REQUIRE(texel.y == Catch::Approx(0.0f).margin(1e-4));
	}
}

// The sequence itself, away from the GPU. A jitter that never leaves the pixel it started on
// antialiases nothing, and one that wanders past the footprint smears -- so the span matters as
// much as the variation, and neither is visible in a velocity readback.
TEST_CASE("The jitter sequence walks one pixel and repeats", "[jitter]")
{
	constexpr float c_W = 256.0f;
	constexpr float c_H = 128.0f;

	// One pixel in NDC, which is what the offsets are expressed in.
	const float pixelX = 2.0f / c_W;
	const float pixelY = 2.0f / c_H;

	std::vector<glm::vec2> offsets;
	for (uint64_t frame = 0; frame < bgl::c_JitterSequenceLength; ++frame)
	{
		offsets.push_back(bgl::HaltonJitter(frame, c_W, c_H));
	}

	auto mean = glm::vec2(0.0f);
	for (const glm::vec2& offset : offsets)
	{
		CHECK(std::abs(offset.x) <= pixelX * 0.5f);
		CHECK(std::abs(offset.y) <= pixelY * 0.5f);
		mean += offset;
	}
	mean /= static_cast<float>(offsets.size());

	// Every term distinct, or the sequence spends frames re-sampling where it has already been.
	for (size_t i = 0; i < offsets.size(); ++i)
	{
		for (size_t j = i + 1; j < offsets.size(); ++j)
		{
			CHECK(glm::distance(offsets[i], offsets[j]) > 1e-6f);
		}
	}

	// No term is the zero offset: that frame would sample exactly where an unjittered one does and
	// contribute nothing new to the accumulation.
	for (const glm::vec2& offset : offsets)
	{
		CHECK(glm::length(offset) > 1e-6f);
	}

	// Balanced about the pixel centre, or the resolved image sits off-centre from the geometry.
	CHECK(mean.x == Catch::Approx(0.0f).margin(pixelX * 0.15f));
	CHECK(mean.y == Catch::Approx(0.0f).margin(pixelY * 0.15f));

	// It is a cycle, so the frame after the last term repeats the first.
	CHECK(
		glm::distance(bgl::HaltonJitter(bgl::c_JitterSequenceLength, c_W, c_H), offsets[0]) <
		1e-6f);
}

// Proof the offset reaches the rasterizer, which no velocity assertion can give: velocity is
// de-jittered by construction, so a jitter that was computed and then dropped on the floor would
// leave every motion test below passing.
//
// Two frames from one camera differ only if the sample grid moved between them.
TEST_CASE("Jitter moves the sampling grid", "[jitter][render]")
{
	const std::string first  = "assets/golden/jitter_frame0.got.png";
	const std::string second = "assets/golden/jitter_frame1.got.png";

	const auto renderTwice = [&](bool taaEnabled) {
		auto fixture = MotionFixture(taaEnabled);
		fixture.AddQuad();

		// Rotated off-axis so the quad's edges cross pixels diagonally; an axis-aligned edge can
		// land on a pixel boundary and survive a sub-pixel shift unchanged.
		const bgl::Camera camera = cameraAt({ 0.0f, 0.0f, c_CameraZ }, glm::radians(12.0f));

		fixture.RenderFrom(camera);
		fixture.gfx->ScreenshotPng(fixture.target, first);
		fixture.RenderFrom(camera);
		fixture.gfx->ScreenshotPng(fixture.target, second);
	};

	SECTION("with temporal AA the two frames differ")
	{
		renderTwice(true);
		CHECK_FALSE(bgl::test::MatchesGolden(first, second));
	}

	SECTION("without it they are the same frame twice")
	{
		renderTwice(false);
		CHECK(bgl::test::MatchesGolden(first, second));
	}
}

// The assertion the whole task turns on. Both clip positions carry a jitter, and the two are
// different offsets, so a velocity that forgot to remove them reports the difference between two
// sample patterns on a scene where nothing moved. The margin is the RG16_FLOAT floor, and a missed
// subtraction is a whole pixel -- 2/256 in NDC, an order of magnitude above it.
TEST_CASE("Jitter leaves a still camera reporting no motion", "[jitter][motionvectors][render]")
{
	auto fixture = MotionFixture(true);
	fixture.AddQuad();
	const auto camera = cameraAt({ 0.0f, 0.0f, c_CameraZ });

	// Four frames, so the pair being differenced is two distinct terms of the sequence rather than
	// the first frame's history-equals-current special case.
	for (int frame = 0; frame < 4; ++frame)
	{
		fixture.RenderFrom(camera);
	}

	const auto motion = fixture.ReadMotionVectors();

	for (const glm::vec2& texel : motion)
	{
		REQUIRE(texel.x == Catch::Approx(0.0f).margin(1e-4));
		REQUIRE(texel.y == Catch::Approx(0.0f).margin(1e-4));
	}
}

// And the moving case: the velocity a camera translation produces must be the one the CPU computes
// from the unjittered cameras, because that is the surface's motion. Same expectation as the
// unjittered reprojection test above, which is the point -- jitter must not show up in the answer.
TEST_CASE(
	"Jitter does not change the velocity a camera translation reports",
	"[jitter][motionvectors][render]")
{
	auto fixture = MotionFixture(true);
	fixture.AddQuad();

	const glm::vec3 eyeBefore{ 0.0f, 0.0f, c_CameraZ };
	const glm::vec3 eyeAfter{ 1.0f, 0.8f, c_CameraZ };

	const bgl::Camera before = cameraAt(eyeBefore);
	const bgl::Camera after  = cameraAt(eyeAfter);

	fixture.RenderFrom(before);
	fixture.RenderFrom(after);

	const glm::vec2 measured = centrePixel(fixture.ReadMotionVectors());

	const glm::vec3 surface  = surfacePointAt(after, eyeAfter, c_Width / 2, c_Height / 2);
	const glm::vec2 expected = projectToUv(after, surface) - projectToUv(before, surface);

	INFO("measured = " << measured.x << ", " << measured.y);
	INFO("expected = " << expected.x << ", " << expected.y);

	CHECK(measured.x == Catch::Approx(expected.x).margin(1e-3));
	CHECK(measured.y == Catch::Approx(expected.y).margin(1e-3));

	CHECK(std::abs(expected.x) > 1e-2f);
	CHECK(std::abs(expected.y) > 1e-2f);
}
