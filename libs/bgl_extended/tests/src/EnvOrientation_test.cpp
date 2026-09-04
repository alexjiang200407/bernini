#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestOptions.h"
#include <assetlib/envmap.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/TextureAssetHandle.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>

// Every other environment test checks the *bake*, and the bake is correct. Nothing checks that a
// world direction in the shader reaches the texel the bake put there -- so a mirrored axis, a
// swapped face or a rotation between the IBL lookup and the skybox's ray would pass the whole suite
// while making a model read flat and lit from nowhere. That is what these cases pin.
//
// The probe is a half-lit environment: radiance 1 wherever a direction has a positive component
// along one world axis, 0 everywhere else. In such an environment a matte white sphere MUST have an
// obvious bright side, and that side must stay locked to the world as the camera orbits.
//
// The assertions are on where the brightness lands *on screen*, never on the cube's texels. Screen
// position is the one ground truth outside the cube convention: if the test built its faces the
// same wrong way the bake does, both the sphere and the backdrop would come out wrong together and
// the check would still fail.

namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	constexpr uint32_t c_SourceFace     = 64;
	constexpr uint32_t c_IrradianceFace = 32;
	constexpr uint32_t c_PrefilterMips  = 7;  // must stay MAX_REFLECTION_LOD + 1

	/**
	 * Direction of texel (col, row) on `face`, in the D3D/Metal cube convention.
	 *
	 * Deliberately a second copy of envmap_bake's faceTexelDir rather than a shared one: this is the
	 * test's own statement of what the convention is, and sharing it would let a change to the bake
	 * silently redefine what the test is asserting.
	 */
	glm::vec3
	FaceTexelDir(uint32_t face, uint32_t col, uint32_t row, uint32_t size)
	{
		const float u = (static_cast<float>(col) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
		const float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;

		switch (face)
		{
		case 0:
			return glm::normalize(glm::vec3(1.0f, -v, -u));
		case 1:
			return glm::normalize(glm::vec3(-1.0f, -v, u));
		case 2:
			return glm::normalize(glm::vec3(u, 1.0f, v));
		case 3:
			return glm::normalize(glm::vec3(u, -1.0f, -v));
		case 4:
			return glm::normalize(glm::vec3(u, -v, 1.0f));
		default:
			return glm::normalize(glm::vec3(-u, -v, -1.0f));
		}
	}

	/** A float cube map, one mip, every texel black. */
	assetlib::ImageData
	MakeCube(uint32_t faceSize)
	{
		auto out      = assetlib::ImageData();
		out.width     = faceSize;
		out.height    = faceSize;
		out.mipLevels = 1;
		out.arraySize = 6;
		out.isCubemap = true;
		out.vkFormat  = assetlib::VkFormat::R32G32B32A32_SFLOAT;
		out.pixels    = core::fixed_buffer<std::byte>(
			static_cast<size_t>(faceSize) * faceSize * 6 * sizeof(float) * 4);

		const auto pitch = static_cast<uint64_t>(faceSize) * sizeof(float) * 4;
		for (uint32_t face = 0; face < 6; ++face)
		{
			out.subresources.push_back(
				{ static_cast<size_t>(pitch) * faceSize * face, pitch, pitch * faceSize });
		}

		return out;
	}

	/** Radiance 1 where `dot(dir, axis) > 0`, 0 elsewhere. */
	assetlib::ImageData
	HalfLitCube(glm::vec3 axis, uint32_t faceSize)
	{
		auto out = MakeCube(faceSize);

		for (uint32_t face = 0; face < 6; ++face)
		{
			auto* dst = reinterpret_cast<float*>(out.pixels.data() + out.subresources[face].offset);

			for (uint32_t row = 0; row < faceSize; ++row)
			{
				for (uint32_t col = 0; col < faceSize; ++col)
				{
					const float lit =
						glm::dot(FaceTexelDir(face, col, row, faceSize), axis) > 0.0f ? 1.0f : 0.0f;
					const size_t t = (static_cast<size_t>(row) * faceSize + col) * 4;

					dst[t + 0] = lit;
					dst[t + 1] = lit;
					dst[t + 2] = lit;
					dst[t + 3] = 1.0f;
				}
			}
		}

		return out;
	}

	struct Probe
	{
		bgl::GraphicsRef        gfx;
		bgl::RenderTargetRef    target;
		bgl::SceneRef           scene;
		bgl::SceneViewRef       view;
		bgl::TextureAssetHandle skybox;
	};

	/** A matte white sphere at the origin, lit only by a half-lit environment about `axis`. */
	Probe
	MakeProbe(glm::vec3 axis)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto probe = Probe();
		probe.gfx  = bgl::CreateGraphics(opts);
		REQUIRE(probe.gfx != nullptr);

		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = static_cast<int>(c_Width);
		targetDesc.height   = static_cast<int>(c_Height);
		targetDesc.headless = true;
		probe.target        = probe.gfx->CreateRenderTarget(targetDesc);
		REQUIRE(probe.target != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 512;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 400000;
		sceneDesc.initialIndices              = 20000;
		sceneDesc.initialPbrMaterials         = 4;

		probe.scene = probe.gfx->CreateScene(sceneDesc);
		probe.view  = probe.gfx->CreateSceneView(probe.scene, 4);

		const auto radiance = HalfLitCube(axis, c_SourceFace);

		auto prefilterDesc      = assetlib::PrefilterDesc();
		prefilterDesc.faceSize  = c_SourceFace;
		prefilterDesc.mipLevels = c_PrefilterMips;
		prefilterDesc.samples   = 32;

		auto irradiance = probe.scene->AddTextureAsset(
			assetlib::irradianceSh(radiance, c_IrradianceFace),
			"probe_irradiance");
		auto prefilter = probe.scene->AddTextureAsset(
			assetlib::prefilterRadiance(radiance, prefilterDesc, nullptr),
			"probe_prefilter");
		auto skybox = probe.scene->AddTextureAsset(HalfLitCube(axis, c_SourceFace), "probe_sky");

		REQUIRE(irradiance.textureSlot);
		REQUIRE(prefilter.textureSlot);
		REQUIRE(skybox.textureSlot);

		probe.skybox = skybox;
		probe.view->SetEnvironmentMap({ irradiance, prefilter });
		probe.view->SetSkyBox(bgl::SkyboxDesc{ skybox });

		// Radiance is already 0..1, so the environment needs no rescaling to survive the tone map.
		probe.view->SetExposure(1.0f);

		const auto matte = probe.scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });

		const auto sphere = probe.scene->AddSphereGeom(32, 32, 5.0f, matte);
		(void)probe.view->CreateStaticMeshInstance(sphere, glm::mat4(1.0f));

		return probe;
	}

	/** Renders `probe` from `eye`, looking at the origin, and leaves the frame at `path`. */
	void
	Shoot(Probe& probe, glm::vec3 eye, const std::string& path)
	{
		auto camera = bgl::Camera();
		camera.LookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = probe.view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		for (int i = 0; i < 6; ++i) probe.gfx->DrawFrame(probe.target, job);

		probe.gfx->ScreenshotPng(probe.target, path);
	}

	// The sphere is radius 5 at the origin, seen from 20 away through a 60 degree vertical field, so
	// its silhouette is about 66 px around (201, 150). These boxes sit near its left and right limbs,
	// where the normal is closest to +-X and the environment's split is at its widest, with several
	// pixels of margin so no corner catches the backdrop.
	constexpr int c_BoxSize = 16;
	constexpr int c_LeftX   = 143;
	constexpr int c_RightX  = 243;
	constexpr int c_BoxY    = 142;

	// Frame corners, outside the sphere, where only the backdrop is drawn.
	constexpr int c_SkyLeftX  = 10;
	constexpr int c_SkyRightX = 360;
	constexpr int c_SkyY      = 20;

	constexpr float c_LitMargin = 1.3f;
}

/**
 * The lit side of a matte sphere is the lit side of the world, from either side of it.
 *
 * A camera on +Z sees world +X on its right; a camera on -Z sees the same world +X on its left. An
 * environment lit only towards +X therefore has to move the sphere's highlight across the frame as
 * the camera crosses to the other side. A lookup that has lost track of world space -- mirrored,
 * transposed, or following the view -- keeps the bright side where it was.
 */
TEST_CASE(
	"IBL shading stays locked to the world as the camera orbits",
	"[pbr][ibl][orientation][render]")
{
	auto probe = MakeProbe(glm::vec3(1.0f, 0.0f, 0.0f));

	const std::string front = "assets/golden/env_orientation_front.got.png";
	const std::string back  = "assets/golden/env_orientation_back.got.png";

	Shoot(probe, glm::vec3(0.0f, 0.0f, 20.0f), front);
	Shoot(probe, glm::vec3(0.0f, 0.0f, -20.0f), back);

	const auto frontLeft  = bgl::test::MeanColor(front, c_LeftX, c_BoxY, c_BoxSize, c_BoxSize);
	const auto frontRight = bgl::test::MeanColor(front, c_RightX, c_BoxY, c_BoxSize, c_BoxSize);
	const auto backLeft   = bgl::test::MeanColor(back, c_LeftX, c_BoxY, c_BoxSize, c_BoxSize);
	const auto backRight  = bgl::test::MeanColor(back, c_RightX, c_BoxY, c_BoxSize, c_BoxSize);

	INFO(
		"front L/R " << frontLeft.Luma() << "/" << frontRight.Luma() << ", back L/R "
					 << backLeft.Luma() << "/" << backRight.Luma());

	// Both boxes are on the sphere, so neither is the empty background.
	REQUIRE(frontRight.Luma() > 0.05f);
	REQUIRE(backLeft.Luma() > 0.05f);

	// The margin is on *display* luma, which is why it is not larger. These normals sit about 48
	// degrees either side of the lit axis, so scene-linear irradiance differs by 4.5x -- and AgX
	// compresses that to about 1.46x by the time it reaches a pixel. A lookup that had lost the
	// world puts both boxes at the same value, so the direction of the inequality is the assertion
	// and the margin only keeps noise out of it.
	CHECK(frontRight.Luma() > frontLeft.Luma() * c_LitMargin);
	CHECK(backLeft.Luma() > backRight.Luma() * c_LitMargin);
}

/**
 * The backdrop and the shading agree about which way the world is lit.
 *
 * Drawn by different code from differently reconstructed directions -- the skybox from a
 * clip-to-world ray, the shading from an interpolated normal -- against cubes baked by one pass. A
 * mismatch between the two is invisible in either alone.
 */
TEST_CASE(
	"The skybox and the IBL are lit from the same direction",
	"[pbr][ibl][orientation][render]")
{
	auto probe = MakeProbe(glm::vec3(1.0f, 0.0f, 0.0f));

	const std::string shot = "assets/golden/env_orientation_sky.got.png";
	Shoot(probe, glm::vec3(0.0f, 0.0f, 20.0f), shot);

	const auto skyLeft     = bgl::test::MeanColor(shot, c_SkyLeftX, c_SkyY, c_BoxSize, c_BoxSize);
	const auto skyRight    = bgl::test::MeanColor(shot, c_SkyRightX, c_SkyY, c_BoxSize, c_BoxSize);
	const auto sphereLeft  = bgl::test::MeanColor(shot, c_LeftX, c_BoxY, c_BoxSize, c_BoxSize);
	const auto sphereRight = bgl::test::MeanColor(shot, c_RightX, c_BoxY, c_BoxSize, c_BoxSize);

	INFO(
		"sky L/R " << skyLeft.Luma() << "/" << skyRight.Luma() << ", sphere L/R "
				   << sphereLeft.Luma() << "/" << sphereRight.Luma());

	CHECK(skyRight.Luma() > skyLeft.Luma() * c_LitMargin);
	CHECK(sphereRight.Luma() > sphereLeft.Luma() * c_LitMargin);
}

/**
 * A sky rotation rotates the lighting with it.
 *
 * `BEnv::skyRotationY` spins the backdrop's ray. Nothing spun the IBL lookup, so a rotated sky used to
 * light the scene from wherever it was authored -- silently, because the only environment shipped
 * has rotationY 0. Half a turn is the loudest case: the backdrop's lit side crosses the frame, and
 * the sphere's has to cross with it.
 */
TEST_CASE("A rotated sky rotates the lighting with it", "[pbr][ibl][orientation][render]")
{
	auto probe = MakeProbe(glm::vec3(1.0f, 0.0f, 0.0f));

	const std::string shot = "assets/golden/env_orientation_rotated.got.png";

	auto rotated          = bgl::SkyboxDesc();
	rotated.skyboxCubeTex = probe.skybox;
	rotated.rotationY     = glm::pi<float>();
	probe.view->SetSkyBox(rotated);

	Shoot(probe, glm::vec3(0.0f, 0.0f, 20.0f), shot);

	const auto skyLeft     = bgl::test::MeanColor(shot, c_SkyLeftX, c_SkyY, c_BoxSize, c_BoxSize);
	const auto skyRight    = bgl::test::MeanColor(shot, c_SkyRightX, c_SkyY, c_BoxSize, c_BoxSize);
	const auto sphereLeft  = bgl::test::MeanColor(shot, c_LeftX, c_BoxY, c_BoxSize, c_BoxSize);
	const auto sphereRight = bgl::test::MeanColor(shot, c_RightX, c_BoxY, c_BoxSize, c_BoxSize);

	INFO(
		"sky L/R " << skyLeft.Luma() << "/" << skyRight.Luma() << ", sphere L/R "
				   << sphereLeft.Luma() << "/" << sphereRight.Luma());

	CHECK(skyLeft.Luma() > skyRight.Luma() * c_LitMargin);
	CHECK(sphereLeft.Luma() > sphereRight.Luma() * c_LitMargin);
}
