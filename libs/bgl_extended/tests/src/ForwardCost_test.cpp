#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LayerType.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <catch2/catch_test_macros.hpp>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <utility>
#include <vector>

// What one full-screen surface of each material kind costs the forward pass at the render grid
// the editor draws a 1146x998 viewport on at 2x -- the grid the cha800 face close-up was measured
// on. Not a test of behaviour: it is run by hand -- `just run bgl_extended_tests --
// "[.forwardcost]"` -- and the numbers are read off the warnings it prints. Layers are stacked in
// depth so every one of them covers every pixel, which is a hair card's worst case rather than
// its average.
namespace
{
	constexpr uint32_t c_Width   = 2292;
	constexpr uint32_t c_Height  = 1996;
	constexpr uint32_t c_TexSize = 256;

	// One mip of flat RGBA8 at the given alpha: every texel partially covers, so a hashed layer
	// keeps about that fraction of its fragments and a mask keeps all or none of them.
	assetlib::ImageData
	FlatTexture(uint8_t alpha)
	{
		auto image      = assetlib::ImageData();
		image.width     = c_TexSize;
		image.height    = c_TexSize;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.isCubemap = false;

		const size_t bytes = static_cast<size_t>(c_TexSize) * c_TexSize * 4;
		image.pixels       = core::fixed_buffer<std::byte>(bytes);
		for (size_t t = 0; t < bytes; t += 4)
		{
			image.pixels[t + 0] = std::byte{ 140 };
			image.pixels[t + 1] = std::byte{ 128 };
			image.pixels[t + 2] = std::byte{ 255 };
			image.pixels[t + 3] = std::byte{ alpha };
		}
		image.subresources.push_back({ 0, static_cast<uint64_t>(c_TexSize) * 4, bytes });
		return image;
	}

	struct CostScene
	{
		bgl::GraphicsRef     gfx;
		bgl::RenderTargetRef target;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderJob       job;
	};

	CostScene
	MakeCostScene(
		bgl::LayerType layer,
		uint32_t       layers,
		bool           opaqueBackdrop,
		uint8_t        alpha,
		uint32_t       segmentsX   = 1,
		uint32_t       segmentsY   = 1,
		float          rollDegrees = 0.0f)
	{
		const glm::mat4 roll =
			glm::rotate(glm::mat4(1.0f), glm::radians(rollDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
		auto opts           = bgl::GraphicsOptions();
		opts.shaderCacheDir = bgl::test::ShaderCacheDir();

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto targetDesc       = bgl::RenderTargetDesc();
		targetDesc.width      = static_cast<int>(c_Width);
		targetDesc.height     = static_cast<int>(c_Height);
		targetDesc.headless   = true;
		targetDesc.taaEnabled = true;
		auto target           = gfx->CreateRenderTarget(targetDesc);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 8;
		sceneDesc.initialMeshlets             = 4096;
		sceneDesc.initialSubmeshes            = 8;
		sceneDesc.initialVertexBufferByteSize = 8000000;
		sceneDesc.initialIndices              = 400000;
		sceneDesc.initialPbrMaterials         = 8;
		auto scene                            = gfx->CreateScene(sceneDesc);
		auto view                             = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		const auto baseColor = scene->AddTextureAsset(FlatTexture(alpha), "cost base");
		const auto normal    = scene->AddTextureAsset(FlatTexture(255), "cost normal");
		const auto orm       = scene->AddTextureAsset(FlatTexture(255), "cost orm");

		// A plane of 60 at z = 0 seen from z = 20 through 60 degrees overfills the frame on both
		// axes, so every pixel is covered by every layer.
		constexpr float c_Plane = 60.0f;

		if (opaqueBackdrop)
		{
			auto desc             = bgl::PbrMaterialDesc();
			desc.baseColorFactor  = glm::vec4(1.0f);
			desc.metallicFactor   = 0.0f;
			desc.roughnessFactor  = 1.0f;
			desc.baseColorTexture = baseColor;
			desc.normalTexture    = normal;
			desc.ormTexture       = orm;
			auto material         = scene->CreatePbrMaterial(desc);
			auto plane = scene->AddPlaneGeom(segmentsX, segmentsY, c_Plane, c_Plane, material);
			view->CreateStaticMeshInstance(
				plane,
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f)) * roll);
		}

		for (uint32_t i = 0; i < layers; ++i)
		{
			auto desc             = bgl::PbrMaterialDesc();
			desc.baseColorFactor  = glm::vec4(1.0f);
			desc.metallicFactor   = 0.0f;
			desc.roughnessFactor  = 1.0f;
			desc.layerType        = layer;
			desc.baseColorTexture = baseColor;
			desc.normalTexture    = normal;
			desc.ormTexture       = orm;
			auto material         = scene->CreatePbrMaterial(desc);
			auto plane = scene->AddPlaneGeom(segmentsX, segmentsY, c_Plane, c_Plane, material);
			view->CreateStaticMeshInstance(
				plane,
				glm::translate(
					glm::mat4(1.0f),
					glm::vec3(0.0f, 0.0f, 0.5f * static_cast<float>(i))) *
					roll);
		}

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 0.0f, 20.0f),
				glm::vec3(0.0f, 0.0f, 19.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		return CostScene{ gfx, target, scene, view, job };
	}

	// The median Forward time over a handful of frames after a warm-up, so one stall does not
	// stand for the cost.
	double
	ForwardMs(CostScene& s)
	{
		s.target->SetGpuTimingEnabled(true);
		std::vector<double> samples;
		for (int frame = 0; frame < 8; ++frame)
		{
			s.gfx->DrawFrame(s.target, s.job);
			s.gfx->WaitIdle();
			if (frame < 3)
				continue;
			for (const bgl::PassTiming& row : s.gfx->GetPassTimings(s.target))
			{
				if (row.name == "Forward 0")
					samples.push_back(row.milliseconds);
			}
		}
		REQUIRE(!samples.empty());
		std::ranges::sort(samples);
		return samples[samples.size() / 2];
	}
}

TEST_CASE("what one full-screen surface of each material kind costs Forward", "[.forwardcost]")
{
	struct Case
	{
		std::string    name;
		bgl::LayerType layer;
		uint32_t       layers;
		bool           backdrop;
		uint8_t        alpha;
		uint32_t       segmentsX   = 1;
		uint32_t       segmentsY   = 1;
		float          rollDegrees = 0.0f;
	};

	const std::vector<Case> cases{
		{ "opaque x1", bgl::LayerType::kOpaque, 0, true, 255 },
		{ "opaque x1 + hashed x3 (alpha 0.5)", bgl::LayerType::kHashed, 3, true, 128 },
		{ "hashed x1 (alpha 0.5)", bgl::LayerType::kHashed, 1, false, 128 },
		{ "hashed x3 (alpha 0.5)", bgl::LayerType::kHashed, 3, false, 128 },
		{ "hashed x3 (alpha 1.0)", bgl::LayerType::kHashed, 3, false, 255 },
		{ "mask x3 (alpha 1.0)", bgl::LayerType::kMask, 3, false, 255 },
		{ "blend x3 (alpha 0.5)", bgl::LayerType::kBlend, 3, false, 128 },
		{ "blend x1 (alpha 0.5)", bgl::LayerType::kBlend, 1, false, 128 },
		// The same coverage cut into many triangles: what a primitive costs the rasterizer beyond
		// the pixels it covers.
		{ "opaque x1, 200x200 segments (80k tris)",
		  bgl::LayerType::kOpaque,
		  0,
		  true,
		  255,
		  200,
		  200 },
		{ "hashed x1 (alpha 1.0), 200x200 segments",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  200,
		  200 },
		{ "hashed x1 (alpha 0.5), 200x200 segments",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  128,
		  200,
		  200 },
		// Thin strips: the same area as slivers whose bounding boxes the rasterizer walks. Axis
		// aligned a strip's box is its area; rolled 45 degrees it is many times it.
		{ "hashed x1, 1x2000 strips, axis aligned",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  1,
		  2000,
		  0.0f },
		{ "hashed x1, 1x2000 strips, rolled 45",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  1,
		  2000,
		  45.0f },
		{ "opaque x1, 1x2000 strips, rolled 45",
		  bgl::LayerType::kOpaque,
		  0,
		  true,
		  255,
		  1,
		  2000,
		  45.0f },
		{ "hashed x1, 1x8000 strips, rolled 45",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  1,
		  8000,
		  45.0f },
		// The same strips cut short along their length, and a quarter as many strips four times as
		// wide: which of the two an asset author can do about it.
		{ "hashed x1, 20x2000 strips, rolled 45",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  20,
		  2000,
		  45.0f },
		{ "hashed x1, 1x500 strips, rolled 45",
		  bgl::LayerType::kHashed,
		  1,
		  false,
		  255,
		  1,
		  500,
		  45.0f },
	};

	for (const Case& c : cases)
	{
		CostScene s = MakeCostScene(
			c.layer,
			c.layers,
			c.backdrop,
			c.alpha,
			c.segmentsX,
			c.segmentsY,
			c.rollDegrees);
		WARN(c.name << ": Forward " << ForwardMs(s) << " ms at " << c_Width << "x" << c_Height);
	}
}
