#include "gfx/RenderTargetBase.h"
#include "util/GoldenImage.h"
#include "util/HalfFloat.h"
#include "util/SkinnedSynth.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include "util/TextureReadback.h"
#include "util/VelocityReadback.h"
#include "util/WaterSurface.h"
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Heightfield.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/StaticMeshGeomDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <bgl/types/TerrainDesc.h>
#include <bgl/types/Viewport.h>
#include <bgpu/types/Barrier.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

/**
 * Forward Water, proven at the pixel. A ramp of ground rises along x through a water plane at
 * y = 2, seen from straight above, and ProbeWater paints what the reader measured: blue where the
 * ground is more than a metre down, green where it is shallower, white within half a metre of the
 * scene behind. So each band is where the reader's depths put it, and a frame proves them.
 *
 * Seen from 28 m above the water, 14.85 pixels span a metre of it, and the frame's middle row is
 * z = 32: world x maps to pixel 320 + (x - 32) * 14.85 along it.
 */

namespace
{
	constexpr uint32_t c_Width  = 640;
	constexpr uint32_t c_Height = 480;

	constexpr float c_WaterLevel = 2.0f;
	constexpr float c_PixelsPerM = 14.85f;
	constexpr int   c_Box        = 6;
	constexpr int   c_CentreRow  = 240;

	[[nodiscard]] int
	PixelX(const float x)
	{
		return static_cast<int>(std::lround(320.0f + (x - 32.0f) * c_PixelsPerM));
	}

	[[nodiscard]] int
	PixelY(const float z)
	{
		return static_cast<int>(std::lround(240.0f + (z - 32.0f) * c_PixelsPerM));
	}

	/** 65 x 65 samples a metre apart: flat at 0 to x = 16, rising 1 in 8 to 4 at x = 48, flat on. */
	assetlib::Heightfield
	Ramp()
	{
		constexpr uint32_t c_Side = 65;
		auto               field  = assetlib::Heightfield();
		field.samplesX            = c_Side;
		field.samplesZ            = c_Side;
		field.cellSize            = 1.0f;
		field.minHeight           = 0.0f;
		field.heightRange         = 4.0f;
		field.heights.resize(static_cast<size_t>(c_Side) * c_Side);
		for (uint32_t z = 0; z < c_Side; ++z)
		{
			for (uint32_t x = 0; x < c_Side; ++x)
			{
				const float height = std::clamp((static_cast<float>(x) - 16.0f) / 8.0f, 0.0f, 4.0f);
				field.heights[z * c_Side + x] = static_cast<uint16_t>(height / 4.0f * 65535.0f);
			}
		}
		return field;
	}

	struct WaterScene
	{
		bgl::GraphicsRef      gfx;
		bgl::RenderTargetRef  target;
		bgl::SceneRef         scene;
		bgl::SceneViewRef     view;
		bgl::MaterialHandle   ground;
		bgl::MaterialHandle   water;
		bgl::GeomHandle       plane;
		assetlib::Heightfield field = Ramp();
		bgl::RenderJob        job;

		WaterScene()
		{
			auto opts                        = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir   = bgl::test::ShaderCacheDir();
			opts.gpuContext.enableDebugLayer = true;
			opts.gpuContext.clientShaderDir  = bgl::test::WaterSurfaceDir();
			gfx                              = bgl::test::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			auto targetDesc     = bgl::RenderTargetDesc();
			targetDesc.width    = static_cast<int>(c_Width);
			targetDesc.height   = static_cast<int>(c_Height);
			targetDesc.headless = true;
			target              = gfx->CreateRenderTarget(targetDesc);
			REQUIRE(target != nullptr);

			auto sceneDesc                    = bgl::SceneDesc();
			sceneDesc.initialPbrMaterials     = 4;
			sceneDesc.initialSurfaceMaterials = 4;
			scene                             = gfx->CreateScene(sceneDesc);
			view                              = gfx->CreateSceneView(scene, 8);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());

			auto groundDesc            = bgl::PbrMaterialDesc();
			groundDesc.metallicFactor  = 0.0f;
			groundDesc.roughnessFactor = 1.0f;
			groundDesc.baseColorFactor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
			ground                     = scene->CreatePbrMaterial(groundDesc);
			(void)scene->CreateTerrain(
				bgl::TerrainDesc().SetHeightfield(&field).SetMaterial(ground));

			water = scene->CreateSurfaceMaterial(
				bgl::SurfaceMaterialDesc{ .surfaceName = "ProbeWater" });
			plane = bgl::test::skinned_synth::AddQuadStaticGeom(*scene, water);

			auto camera = bgl::Camera();
			camera
				.LookAt(
					glm::vec3(32.0f, 30.0f, 32.0f),
					glm::vec3(32.0f, 0.0f, 32.0f),
					glm::vec3(0.0f, 0.0f, -1.0f))
				.Perspective(
					glm::radians(60.0f),
					static_cast<float>(c_Width) / static_cast<float>(c_Height),
					0.1f,
					500.0f);

			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
		}

		/** The sea: the unit quad laid flat at the water level over the whole field. */
		void
		Flood(const bgl::SceneViewRef& into) const
		{
			const glm::mat4 transform =
				glm::translate(glm::mat4(1.0f), glm::vec3(32.0f, c_WaterLevel, 32.0f)) *
				glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(40.0f));
			into->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(plane).SetTransform(transform));
		}

		/** Renders `frame` and keeps the PNG for the boxes a case reads. */
		[[nodiscard]] std::string
		Capture(const bgl::RenderJob& frame, const char* name) const
		{
			const auto path =
				(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();
			gfx->DrawFrame(target, frame);
			gfx->ScreenshotPng(target, path);
			return path;
		}
	};

	[[nodiscard]] bgl::test::Rgba
	At(const std::string& path, const int x, const int y)
	{
		return bgl::test::MeanColor(path, x - c_Box / 2, y - c_Box / 2, c_Box, c_Box);
	}

	[[nodiscard]] bool
	Blue(const bgl::test::Rgba& c)
	{
		return c.b > c.r + 0.2f && c.b > c.g + 0.2f;
	}

	[[nodiscard]] bool
	Green(const bgl::test::Rgba& c)
	{
		return c.g > c.r + 0.2f && c.g > c.b + 0.2f;
	}

	[[nodiscard]] bool
	White(const bgl::test::Rgba& c)
	{
		return std::min({ c.r, c.g, c.b }) > 0.6f;
	}
}

TEST_CASE("Water draws in bands of how deep it is, and not over dry ground", "[water][render]")
{
	const WaterScene  sea;
	const std::string dry = sea.Capture(sea.job, "bernini_water_dry");
	sea.Flood(sea.view);
	const std::string wet = sea.Capture(sea.job, "bernini_water_wet");

	// Ground two metres down and more: deep.
	CHECK(Blue(At(wet, PixelX(20.0f), c_CentreRow)));
	// Between one and a half metres down: shallow, by the ground under it.
	CHECK(Green(At(wet, PixelX(27.0f), c_CentreRow)));
	// The last half metre before the shore: foam, by the scene behind along the view ray.
	CHECK(White(At(wet, PixelX(30.5f), c_CentreRow)));

	// Past the shore the ground stands above the water, and hides it: the pixel is the dry frame's.
	const bgl::test::Rgba above = At(wet, PixelX(40.0f), c_CentreRow);
	const bgl::test::Rgba bare  = At(dry, PixelX(40.0f), c_CentreRow);
	CHECK(std::abs(above.r - bare.r) < 0.01f);
	CHECK(std::abs(above.g - bare.g) < 0.01f);
	CHECK(std::abs(above.b - bare.b) < 0.01f);
	CHECK_FALSE(Blue(bare));

	std::filesystem::remove(dry);
	std::filesystem::remove(wet);
}

TEST_CASE("A mesh standing in the water gets a ring of foam", "[water][render]")
{
	const WaterScene sea;
	sea.Flood(sea.view);

	// A ball sunk three metres below the water to its centre: its surface rises through the water
	// in a circle 2.65 m across, and lies within half a metre under it out to 3.12 m.
	auto ballDesc           = bgl::PbrMaterialDesc();
	ballDesc.metallicFactor = 0.0f;
	const auto ball =
		sea.scene->AddSphereGeom(32, 32, 4.0f, sea.scene->CreatePbrMaterial(ballDesc));
	const glm::vec3 centre = glm::vec3(16.0f, c_WaterLevel - 3.0f, 24.0f);
	sea.view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(ball).SetTransform(
			glm::translate(glm::mat4(1.0f), centre)));

	const std::string frame = sea.Capture(sea.job, "bernini_water_ring");

	// The reach is measured along the view ray, so the ring is widest on the side facing the camera
	// -- here +x -- and thins to the ball's own edge on the far side.
	const int row = PixelY(centre.z);
	CHECK(White(At(frame, PixelX(centre.x + 2.9f), row)));
	// Clear of the ring on either side, the same water is deep.
	CHECK(Blue(At(frame, PixelX(centre.x + 6.0f), row)));
	CHECK(Blue(At(frame, PixelX(centre.x - 4.5f), row)));

	std::filesystem::remove(frame);
}

TEST_CASE("Still water under a still camera writes no motion", "[water][render][motionvectors]")
{
	const WaterScene sea;
	sea.Flood(sea.view);

	sea.gfx->DrawFrame(sea.target, sea.job);
	sea.gfx->DrawFrame(sea.target, sea.job);

	const std::vector<glm::vec4> motion =
		bgl::test::ReadVelocityTexels(sea.gfx.Get(), sea.target.Get(), c_Width, c_Height);
	for (const glm::vec4& texel : motion)
	{
		REQUIRE(std::abs(texel.x) < 1e-4f);
		REQUIRE(std::abs(texel.y) < 1e-4f);
	}
}

TEST_CASE(
	"Water leaves the TAA marker as the ground under it wrote it",
	"[water][render][taaghosting]")
{
	// The ground is opaque and writes 1; water writes no depth, so the depth the marker vouches for
	// is still the ground's, and the marker stays.
	const WaterScene sea;
	sea.Flood(sea.view);
	sea.gfx->DrawFrame(sea.target, sea.job);

	auto*                      base  = sea.target->As<bgl::RenderTargetBase>();
	const std::vector<uint8_t> bytes = bgl::test::ReadTextureBytes(
		sea.gfx.Get(),
		base->GetSceneColorTexture(),
		c_Width,
		c_Height,
		8,
		bgpu::BarrierLayout::kShaderResource);

	for (const float x : { 20.0f, 27.0f, 30.5f })
	{
		const size_t pixel = static_cast<size_t>(c_CentreRow) * c_Width + PixelX(x);
		uint16_t     alpha = 0;
		std::memcpy(&alpha, bytes.data() + pixel * 8 + 6, sizeof(alpha));
		CHECK(bgl::test::HalfToFloat(alpha) == 1.0f);
	}
}

TEST_CASE("A second view's water reads the scene's terrain", "[water][render]")
{
	// The terrain is the scene's and a placement the view's: a view with water of its own shades it
	// over the same ground, in the same bands.
	const WaterScene sea;
	auto             other = sea.gfx->CreateSceneView(sea.scene, 8);
	bgl::test::ApplyEnvironment(sea.scene.Get(), other.Get());
	sea.Flood(other);

	auto job                = sea.job;
	job.view                = other;
	const std::string frame = sea.Capture(job, "bernini_water_other_view");

	CHECK(Blue(At(frame, PixelX(20.0f), c_CentreRow)));
	CHECK(Green(At(frame, PixelX(27.0f), c_CentreRow)));

	std::filesystem::remove(frame);
}

namespace
{
	/** One quad of two triangles in the z = 0 plane, spanning x0..x1 and -1..1, drawn by material 0. */
	void
	AppendQuad(assetlib::BMesh& mesh, const float x0, const float x1)
	{
		constexpr uint16_t c_Stride = 12;

		const std::array<glm::vec3, 4> corners = {
			glm::vec3(x0, -1.0f, 0.0f),
			glm::vec3(x1, -1.0f, 0.0f),
			glm::vec3(x1, 1.0f, 0.0f),
			glm::vec3(x0, 1.0f, 0.0f),
		};
		const auto byteOffset = static_cast<uint32_t>(mesh.vertexData.size());
		mesh.vertexData.resize(byteOffset + sizeof(corners));
		std::memcpy(mesh.vertexData.data() + byteOffset, corners.data(), sizeof(corners));

		auto meshlet            = assetlib::Meshlet();
		meshlet.vertexOffset    = static_cast<uint32_t>(mesh.meshletVertices.size());
		meshlet.triangleOffset  = static_cast<uint32_t>(mesh.meshletTriangles.size());
		meshlet.vertexCount     = 4;
		meshlet.triangleCount   = 2;
		meshlet.boundingRadius  = 2.0f;
		const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
		mesh.meshlets.push_back(meshlet);
		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		constexpr std::array<uint8_t, 6> c_Triangles = { { 0, 1, 2, 0, 2, 3 } };
		mesh.meshletTriangles.insert(
			mesh.meshletTriangles.end(),
			c_Triangles.begin(),
			c_Triangles.end());

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexByteOffset      = byteOffset;
		submesh.vertexCount           = 4;
		submesh.firstMeshlet          = firstMeshlet;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(x0, -1.0f, 0.0f);
		submesh.aabbMax               = glm::vec3(x1, 1.0f, 0.0f);
		mesh.submeshes.push_back(submesh);
	}
}

TEST_CASE(
	"Water with levels of detail dissolves between them, over the sky and no ground",
	"[water][render][lod]")
{
	// LodRender_test's split levels drawn as water: level 0 the left quad, level 1 the right, seen
	// against the empty background with no terrain in the scene. Nothing is behind the water and no
	// ground is under it, so both depths are past any shore and every pixel is deep -- never foam --
	// and a change of level dissolves through each level's own water pipeline.
	constexpr int c_Size = 64;
	constexpr int c_BoxY = 20, c_BoxH = 24, c_BoxW = 10;
	constexpr int c_LeftX = 18, c_RightX = 36;

	auto opts                       = bgl::test::GraphicsSetup();
	opts.gpuContext.shaderCacheDir  = bgl::test::ShaderCacheDir();
	opts.gpuContext.clientShaderDir = bgl::test::WaterSurfaceDir();
	auto gfx                        = bgl::test::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto sceneDesc                    = bgl::SceneDesc();
	sceneDesc.initialSurfaceMaterials = 4;
	auto scene                        = gfx->CreateScene(sceneDesc);
	auto view                         = gfx->CreateSceneView(scene, 4);

	const bgl::MaterialHandle water =
		scene->CreateSurfaceMaterial(bgl::SurfaceMaterialDesc{ .surfaceName = "ProbeWater" });

	auto levels = assetlib::BMesh();
	AppendQuad(levels, -1.0f, -0.1f);
	AppendQuad(levels, 0.1f, 1.0f);
	levels.meshes.push_back(
		assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0, .lodCount = 2 });
	levels.lods     = { { 20.0f }, { 0.0f } };
	const auto geom = scene->AddStaticMeshGeom(
		bgl::StaticMeshGeomDesc().SetMesh(&levels).SetMaterials({ &water, 1 }));
	REQUIRE(geom.IsValid());
	view->CreateStaticMeshInstance(
		bgl::StaticMeshInstanceDesc().SetGeom(geom).SetTransform(
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f))));

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = c_Size;
	targetDesc.height     = c_Size;
	targetDesc.headless   = true;
	targetDesc.taaEnabled = false;
	auto target           = gfx->CreateRenderTarget(targetDesc);

	float      time  = 0.0f;
	const auto frame = [&](const std::string& name) {
		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
		job.camera =
			bgl::Camera()
				.LookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
		job.time = time;
		gfx->DrawFrame(target, job);
		time += 0.03f;

		const std::string path =
			(std::filesystem::temp_directory_path() / ("bernini_water_lod_" + name + ".png"))
				.string();
		gfx->ScreenshotPng(target, path);
		const bgl::test::Rgba near = bgl::test::MeanColor(path, c_LeftX, c_BoxY, c_BoxW, c_BoxH);
		const bgl::test::Rgba far  = bgl::test::MeanColor(path, c_RightX, c_BoxY, c_BoxW, c_BoxH);
		std::filesystem::remove(path);
		return std::array<bgl::test::Rgba, 2>{ near, far };
	};

	auto select = [&view](const float pixelScale) {
		auto desc       = bgl::LodSelectionDesc();
		desc.pixelScale = pixelScale;
		view->SetLodSelection(desc);
	};

	select(4.0f);
	const auto before = frame("before");
	REQUIRE(Blue(before[1]));
	const float full = before[1].b;
	CHECK(before[0].b < 0.05f);

	select(1.0f);
	for (int step = 1; step <= 4; ++step)
	{
		const auto shares = frame("dissolve" + std::to_string(step));
		INFO("frame " << step);
		CHECK(shares[0].b > 0.05f * full);
		CHECK(shares[1].b > 0.05f * full);
		CHECK_FALSE(White(shares[0]));
		CHECK_FALSE(White(shares[1]));
	}

	const auto after = frame("after");
	CHECK(Blue(after[0]));
	CHECK(after[0].b == Catch::Approx(full).margin(0.02f));
	CHECK(after[1].b < 0.05f);
}
