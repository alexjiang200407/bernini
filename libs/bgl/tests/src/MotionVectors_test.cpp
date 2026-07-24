#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "gfx/RenderTargetBase.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "util/GpuValidation.h"
#include "util/TestOptions.h"
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
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

	float
	halfToFloat(uint16_t bits)
	{
		const uint32_t sign     = static_cast<uint32_t>(bits & 0x8000u) << 16;
		const uint32_t exponent = (bits >> 10) & 0x1Fu;
		const uint32_t mantissa = bits & 0x3FFu;

		if (exponent == 0)
		{
			if (mantissa == 0)
			{
				return std::bit_cast<float>(sign);
			}

			// Subnormal: renormalize by shifting the mantissa up until its leading bit falls out.
			uint32_t e = 1;
			uint32_t m = mantissa;
			while ((m & 0x400u) == 0)
			{
				m <<= 1;
				++e;
			}
			m &= 0x3FFu;
			return std::bit_cast<float>(sign | ((127 - 15 - e + 1) << 23) | (m << 13));
		}

		if (exponent == 0x1Fu)
		{
			return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
		}

		return std::bit_cast<float>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
	}

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

		MotionFixture()
		{
			auto opts                     = bgl::GraphicsOptions();
			opts.shaderCacheDir           = bgl::test::ShaderCacheDir();
			opts.enableDebugLayer         = true;
			opts.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

			gfx = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = static_cast<int>(c_Width);
			targetDesc.height   = static_cast<int>(c_Height);
			targetDesc.headless = true;

			target = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			targetBase = target->As<bgl::RenderTargetBase>();
			REQUIRE(targetBase != nullptr);

			auto sceneDesc                    = bgl::SceneDesc();
			sceneDesc.maxGeom                 = 4;
			sceneDesc.maxMeshlets             = 64;
			sceneDesc.maxSubmeshes            = 4;
			sceneDesc.maxVertexBufferByteSize = 8192;
			sceneDesc.maxIndices              = 256;

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
		void
		AddQuad()
		{
			auto plane = scene->AddPlaneGeom(1, 1, c_QuadExtent * 2.0f, c_QuadExtent * 2.0f);
			view->CreateStaticMeshInstance(
				plane,
				glm::translate(glm::mat4(1.0f), { 0, 0, c_PlaneZ }));
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
					motion[static_cast<size_t>(y) * c_Width + x] =
						glm::vec2(halfToFloat(row[x * 2]), halfToFloat(row[x * 2 + 1]));
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
