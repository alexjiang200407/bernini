#include "util/GoldenImage.h"
#include "util/SyntheticCube.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VertexPacking.h"
#include <array>
#include <assetlib/envmap.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <assetlib_structs/VkFormat.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/TextureAssetHandle.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/LoosePbrMaterialDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/SurfaceMaterialDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/containers/fixed_buffer.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// A plane wider than the frame, facing the camera, whose second UV set runs across it left to right
// once. Its occlusion map is eight texels in one row: the left four fully occluded, the right four
// not, so the left box lands on 0 and the right box on 1 with the filter's transition between them.
// Every assertion compares a box against the same box rendered without the map, so nothing depends on
// the plane being evenly lit.
namespace
{
	constexpr uint32_t c_Width  = 400;
	constexpr uint32_t c_Height = 300;

	// Screen x 100 and 300 are world x -7.7 and +7.7 at this camera, which is u 0.31 and 0.69 across
	// the plane: inside the flat run of each half of the map, clear of the transition at 0.5.
	constexpr int c_BoxSize   = 16;
	constexpr int c_BoxY      = 142;
	constexpr int c_OccludedX = 92;
	constexpr int c_OpenX     = 292;

	constexpr float c_HalfExtent = 20.0f;
	constexpr float c_Albedo     = 0.5f;

	// DirectionalLight_test's margin, in display luma.
	constexpr float c_LevelMargin = 0.015f;

	constexpr uint32_t c_SourceFace     = 64;
	constexpr uint32_t c_IrradianceFace = 32;
	constexpr uint32_t c_PrefilterMips  = 7;

	/** One quad at z = 0 facing +Z, with a second UV set when `withUv1`. */
	assetlib::BMesh
	MakePlane(bool withUv1)
	{
		const uint16_t stride = withUv1 ? 40 : 32;

		auto mesh = assetlib::BMesh();

		const std::array<glm::vec2, 4> corners = { glm::vec2(-1.0f, -1.0f),
			                                       glm::vec2(1.0f, -1.0f),
			                                       glm::vec2(1.0f, 1.0f),
			                                       glm::vec2(-1.0f, 1.0f) };

		mesh.vertexData.resize(corners.size() * stride);
		for (size_t i = 0; i < corners.size(); ++i)
		{
			const glm::vec2 c  = corners[i];
			const glm::vec2 uv = c * 0.5f + 0.5f;

			const std::array<float, 3> position = {
				{ c.x * c_HalfExtent, c.y * c_HalfExtent, 0.0f }
			};
			const std::array<float, 3> normal   = { { 0.0f, 0.0f, 1.0f } };
			const std::array<float, 2> texcoord = { { uv.x, uv.y } };

			const size_t at = i * stride;
			bgl::test::PutFloats(mesh.vertexData, at, position);
			bgl::test::PutFloats(mesh.vertexData, at + 12, normal);
			bgl::test::PutFloats(mesh.vertexData, at + 24, texcoord);
			if (withUv1)
				bgl::test::PutFloats(mesh.vertexData, at + 32, texcoord);
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexOffset   = 0;
		meshlet.triangleOffset = 0;
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingCenter = glm::vec3(0.0f);
		meshlet.boundingRadius = c_HalfExtent * 1.5f;
		mesh.meshlets.push_back(meshlet);

		for (const uint32_t v : { 0u, 1u, 2u, 3u }) mesh.meshletVertices.push_back(v);
		for (const uint32_t t : { 0u, 1u, 2u, 0u, 2u, 3u })
			mesh.meshletTriangles.push_back(static_cast<uint8_t>(t));

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = withUv1 ? 4 : 3;
		submesh.layout.stride         = stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kTexCoord0,
			                              assetlib::VertexFormat::kFloat32x2,
			                              24 };
		submesh.layout.attributes[3]  = { assetlib::VertexSemantic::kTexCoord1,
			                              assetlib::VertexFormat::kFloat32x2,
			                              32 };
		submesh.vertexByteOffset      = 0;
		submesh.vertexCount           = 4;
		submesh.firstMeshlet          = 0;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-c_HalfExtent, -c_HalfExtent, 0.0f);
		submesh.aabbMax               = glm::vec3(c_HalfExtent, c_HalfExtent, 0.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	/** Eight texels in a row, R = 0 on the left half and 1 on the right. */
	assetlib::ImageData
	HalfOccludedMap()
	{
		constexpr uint32_t c_Texels = 8;

		auto image      = assetlib::ImageData();
		image.width     = c_Texels;
		image.height    = 1;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;
		image.isCubemap = false;

		const size_t bytes = static_cast<size_t>(c_Texels) * 4;
		image.pixels       = core::fixed_buffer<std::byte>(bytes);
		for (uint32_t t = 0; t < c_Texels; ++t)
		{
			const auto value        = std::byte{ t < c_Texels / 2 ? uint8_t{ 0 } : uint8_t{ 255 } };
			image.pixels[t * 4 + 0] = value;
			image.pixels[t * 4 + 1] = value;
			image.pixels[t * 4 + 2] = value;
			image.pixels[t * 4 + 3] = std::byte{ 255 };
		}
		image.subresources.push_back({ 0, static_cast<uint64_t>(c_Texels) * 4, bytes });
		return image;
	}

	enum class Light
	{
		kEnvironment,  // the shipped environment and no sun: every photon is indirect
		kSun,          // a black environment and a head-on sun: every photon is direct
	};

	struct Probe
	{
		bgl::GraphicsRef        gfx;
		bgl::RenderTargetRef    target;
		bgl::SceneRef           scene;
		bgl::SceneViewRef       view;
		bgl::MeshInstanceHandle plane;
		bgl::TextureAssetHandle map;
	};

	Probe
	MakeProbe(bool withUv1, Light light)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;
		opts.surfaceShaderDir = "./shaders/tests/surfaces";

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
		sceneDesc.initialMeshlets             = 16;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 4096;
		sceneDesc.initialIndices              = 64;
		sceneDesc.initialPbrMaterials         = 16;
		sceneDesc.initialSurfaceMaterials     = 8;

		probe.scene = probe.gfx->CreateScene(sceneDesc);
		probe.view  = probe.gfx->CreateSceneView(probe.scene, 4);

		if (light == Light::kSun)
		{
			auto prefilterDesc      = assetlib::PrefilterDesc();
			prefilterDesc.faceSize  = c_SourceFace;
			prefilterDesc.mipLevels = c_PrefilterMips;
			prefilterDesc.samples   = 32;

			const auto radiance = bgl::test::MakeBlackFloatCube(c_SourceFace);

			probe.view->SetEnvironmentMap(
				{ probe.scene->AddTextureAsset(
					  assetlib::irradianceSh(radiance, c_IrradianceFace),
					  "black_irradiance"),
			      probe.scene->AddTextureAsset(
					  assetlib::prefilterRadiance(radiance, prefilterDesc, nullptr),
					  "black_prefilter") });
			probe.view->SetExposure(1.0f);
			probe.view->SetDirectionalLight(
				{ .direction = glm::vec3(0.0f, 0.0f, -1.0f),
			      .color     = glm::vec3(1.0f),
			      .intensity = 0.6f });
		}
		else
		{
			bgl::test::ApplyEnvironment(probe.scene.Get(), probe.view.Get());
		}

		probe.map = probe.scene->AddTextureAsset(HalfOccludedMap(), "half_occluded");

		const auto placeholder = probe.scene->CreatePbrMaterial({});
		const auto geom        = probe.scene->AddStaticMeshGeom(
			MakePlane(withUv1),
			0,
			std::span<const bgl::MaterialHandle>(&placeholder, 1));
		probe.plane = probe.view->CreateStaticMeshInstance(geom, glm::mat4(1.0f));

		return probe;
	}

	/** The two boxes' mean colours with `material` on the plane. */
	std::array<bgl::test::Rgba, 2>
	Shoot(Probe& probe, bgl::MaterialHandle material, const std::string& name)
	{
		probe.view->SetSubmeshMaterialOverride(probe.plane, 0, material);

		auto camera = bgl::Camera();
		camera.LookAt(glm::vec3(0.0f, 0.0f, 20.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				500.0f);

		auto job     = bgl::RenderJob();
		job.view     = probe.view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		// Enough frames for TAA to blend the previous shot's material out of its history.
		for (int i = 0; i < 24; ++i) probe.gfx->DrawFrame(probe.target, job);

		const auto shot = "assets/golden/uv1_occlusion_" + name + ".got.png";
		probe.gfx->ScreenshotPng(probe.target, shot);

		return { { bgl::test::MeanColor(shot, c_OccludedX, c_BoxY, c_BoxSize, c_BoxSize),
			       bgl::test::MeanColor(shot, c_OpenX, c_BoxY, c_BoxSize, c_BoxSize) } };
	}

	/** One material of each kind, matte and with no specular lobe, carrying `map` or none. */
	struct Kind
	{
		const char*                                                               name;
		std::function<bgl::MaterialHandle(bgl::IScene&, bgl::TextureAssetHandle)> create;
	};

	std::vector<Kind>
	Kinds()
	{
		return {
			{ "pbr",
			  [](bgl::IScene& scene, bgl::TextureAssetHandle map) {
				  return scene.CreatePbrMaterial(
					  { .baseColorFactor     = glm::vec4(c_Albedo, c_Albedo, c_Albedo, 1.0f),
			            .metallicFactor      = 0.0f,
			            .roughnessFactor     = 1.0f,
			            .specularFactor      = 0.0f,
			            .uv1OcclusionTexture = map });
			  } },
			{ "loose",
			  [](bgl::IScene& scene, bgl::TextureAssetHandle map) {
				  auto desc                = bgl::LoosePbrMaterialDesc();
				  desc.baseColorFactor     = glm::vec4(c_Albedo, c_Albedo, c_Albedo, 1.0f);
				  desc.metallicFactor      = 0.0f;
				  desc.roughnessFactor     = 1.0f;
				  desc.specularFactor      = 0.0f;
				  desc.uv1OcclusionTexture = map;
				  return scene.CreateLoosePbrMaterial(desc);
			  } },
			{ "surface",
			  [](bgl::IScene& scene, bgl::TextureAssetHandle map) {
				  auto desc                = bgl::SurfaceMaterialDesc();
				  desc.surface             = "PbrLike";
				  desc.values              = { { "baseColorFactor",
				                                 glm::vec4(c_Albedo, c_Albedo, c_Albedo, 1.0f) },
				                               { "roughnessFactor", glm::vec4(1.0f) },
				                               { "metallicFactor", glm::vec4(0.0f) } };
				  desc.uv1OcclusionTexture = map;
				  return scene.CreateSurfaceMaterial(desc);
			  } },
		};
	}
}

TEST_CASE(
	"A UV1 occlusion map darkens the environment's light and not the sun's",
	"[uv1ao][render]")
{
	SECTION(
		"under the environment alone, the occluded half goes dark and the open half is untouched")
	{
		auto probe = MakeProbe(true, Light::kEnvironment);

		for (const Kind& kind : Kinds())
		{
			INFO("material kind " << kind.name);

			const auto plain =
				Shoot(probe, kind.create(*probe.scene, {}), std::string(kind.name) + "_env_plain");
			const auto mapped = Shoot(
				probe,
				kind.create(*probe.scene, probe.map),
				std::string(kind.name) + "_env_mapped");

			INFO("occluded " << mapped[0].Luma() << " against " << plain[0].Luma());
			INFO("open " << mapped[1].Luma() << " against " << plain[1].Luma());

			// A lit plane, so the comparisons below are about light and not about a missed box.
			REQUIRE(plain[0].Luma() > 0.1f);

			CHECK(mapped[0].Luma() < plain[0].Luma() * 0.25f);
			CHECK(std::abs(mapped[1].Luma() - plain[1].Luma()) < c_LevelMargin);
		}
	}

	// The sun is scaled by no AO (PbrShading.slang): what occludes a directional light is a shadow.
	SECTION("under the sun alone, the map changes nothing")
	{
		auto probe = MakeProbe(true, Light::kSun);

		for (const Kind& kind : Kinds())
		{
			INFO("material kind " << kind.name);

			const auto plain =
				Shoot(probe, kind.create(*probe.scene, {}), std::string(kind.name) + "_sun_plain");
			const auto mapped = Shoot(
				probe,
				kind.create(*probe.scene, probe.map),
				std::string(kind.name) + "_sun_mapped");

			INFO("occluded " << mapped[0].Luma() << " against " << plain[0].Luma());
			INFO("open " << mapped[1].Luma() << " against " << plain[1].Luma());

			REQUIRE(plain[0].Luma() > 0.1f);

			CHECK(std::abs(mapped[0].Luma() - plain[0].Luma()) < c_LevelMargin);
			CHECK(std::abs(mapped[1].Luma() - plain[1].Luma()) < c_LevelMargin);
		}
	}
}

// A material is shared across meshes, and only some of them carry the set it addresses: the rest
// must draw as though the map were white rather than stamped with whatever one texel holds.
TEST_CASE(
	"A mesh without a second UV set reads a UV1 occlusion map as unoccluded",
	"[uv1ao][render]")
{
	auto probe = MakeProbe(false, Light::kEnvironment);

	for (const Kind& kind : Kinds())
	{
		INFO("material kind " << kind.name);

		const auto plain =
			Shoot(probe, kind.create(*probe.scene, {}), std::string(kind.name) + "_nouv1_plain");
		const auto mapped = Shoot(
			probe,
			kind.create(*probe.scene, probe.map),
			std::string(kind.name) + "_nouv1_mapped");

		INFO("left " << mapped[0].Luma() << " against " << plain[0].Luma());
		INFO("right " << mapped[1].Luma() << " against " << plain[1].Luma());

		REQUIRE(plain[0].Luma() > 0.1f);

		CHECK(std::abs(mapped[0].Luma() - plain[0].Luma()) < c_LevelMargin);
		CHECK(std::abs(mapped[1].Luma() - plain[1].Luma()) < c_LevelMargin);
	}
}
