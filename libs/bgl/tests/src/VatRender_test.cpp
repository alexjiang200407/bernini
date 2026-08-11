#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib_structs/ImageData.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// The synthesized VAT: a 4-vertex quad (4 columns) and two clips stacked along V, each padded
	// with a duplicate terminal row exactly as the bake writes one.
	//   clip 0, frame 0: the bind-pose quad. frame 1: the top edge sheared +X.
	//   clip 1, frame 0: the quad at half size.
	// Rows: [flat, sheared, sheared(pad), half, half(pad)].
	constexpr uint32_t c_Columns = 4;
	constexpr uint32_t c_Rows    = 5;

	const glm::vec3 c_BoundsMin(-1.5f, -1.5f, -1.0f);
	const glm::vec3 c_BoundsMax(2.5f, 1.5f, 1.0f);

	// Corner order (also column order): bottom-left, bottom-right, top-left, top-right.
	const std::array<glm::vec3, 4> c_FlatQuad = { {
		{ -1.0f, -1.0f, 0.0f },
		{ 1.0f, -1.0f, 0.0f },
		{ -1.0f, 1.0f, 0.0f },
		{ 1.0f, 1.0f, 0.0f },
	} };

	const std::array<glm::vec3, 4> c_ShearedQuad = { {
		{ -1.0f, -1.0f, 0.0f },
		{ 1.0f, -1.0f, 0.0f },
		{ 0.5f, 1.0f, 0.0f },
		{ 2.5f, 1.0f, 0.0f },
	} };

	const std::array<glm::vec3, 4> c_HalfQuad = { {
		{ -0.5f, -0.5f, 0.0f },
		{ 0.5f, -0.5f, 0.0f },
		{ -0.5f, 0.5f, 0.0f },
		{ 0.5f, 0.5f, 0.0f },
	} };

	uint16_t
	PackUnorm16(float value)
	{
		return static_cast<uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 65535.0f));
	}

	void
	WritePositionRow(assetlib::ImageData& image, uint32_t row, std::span<const glm::vec3> corners)
	{
		auto* texel = reinterpret_cast<uint16_t*>(
			image.pixels.data() + uint64_t(row) * image.subresources[0].rowPitch);

		const glm::vec3 extent = c_BoundsMax - c_BoundsMin;
		for (const glm::vec3& corner : corners)
		{
			const glm::vec3 packed = (corner - c_BoundsMin) / extent;
			texel[0]               = PackUnorm16(packed.x);
			texel[1]               = PackUnorm16(packed.y);
			texel[2]               = PackUnorm16(packed.z);
			texel[3]               = 65535;
			texel += 4;
		}
	}

	assetlib::ImageData
	MakeImage(assetlib::VkFormat format, uint32_t texelBytes)
	{
		auto image      = assetlib::ImageData();
		image.width     = c_Columns;
		image.height    = c_Rows;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = format;

		const uint64_t pitch = uint64_t(c_Columns) * texelBytes;
		image.pixels         = core::fixed_buffer<std::byte>(pitch * c_Rows);
		image.subresources.push_back({ 0, pitch, pitch * c_Rows });
		return image;
	}

	assetlib::ImageData
	MakePositionTexture()
	{
		auto image = MakeImage(assetlib::VkFormat::R16G16B16A16_UNORM, 8);

		WritePositionRow(image, 0, c_FlatQuad);
		WritePositionRow(image, 1, c_ShearedQuad);
		WritePositionRow(image, 2, c_ShearedQuad);  // clip 0's padding row
		WritePositionRow(image, 3, c_HalfQuad);
		WritePositionRow(image, 4, c_HalfQuad);  // clip 1's padding row
		return image;
	}

	assetlib::ImageData
	MakeNormalTexture()
	{
		auto image = MakeImage(assetlib::VkFormat::R8G8B8A8_UNORM, 4);

		// +Z everywhere: the quads never tilt, only translate in their plane.
		for (size_t i = 0; i < image.pixels.size(); i += 4)
		{
			image.pixels[i + 0] = std::byte{ 128 };
			image.pixels[i + 1] = std::byte{ 128 };
			image.pixels[i + 2] = std::byte{ 255 };
			image.pixels[i + 3] = std::byte{ 255 };
		}
		return image;
	}

	std::vector<bgl::VatVertex>
	MakeQuadVertices()
	{
		auto verts = std::vector<bgl::VatVertex>(4);
		for (size_t i = 0; i < 4; ++i)
		{
			verts[i].position = c_FlatQuad[i];
			verts[i].normal   = glm::vec3(0.0f, 0.0f, 1.0f);
			verts[i].uv       = glm::vec2(i % 2 == 0 ? 0.0f : 1.0f, i < 2 ? 1.0f : 0.0f);
			verts[i].tangent  = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		}
		return verts;
	}
}

TEST_CASE("VAT instances draw the frame they were frozen at", "[vat][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);
	REQUIRE(target != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	// PBR does not render without an environment; there is no default.
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const auto positions = scene->AddTextureAsset(MakePositionTexture(), "vat-positions");
	const auto normals   = scene->AddTextureAsset(MakeNormalTexture(), "vat-normals");

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	auto desc      = bgl::VatGeomDesc();
	desc.positions = positions;
	desc.normals   = normals;
	desc.boundsMin = c_BoundsMin;
	desc.boundsMax = c_BoundsMax;
	desc.clips     = { { 0, 2, 30.0f, false }, { 3, 1, 30.0f, false } };

	const auto verts   = MakeQuadVertices();
	const auto indices = std::array<uint32_t, 6>{ { 0, 1, 2, 2, 1, 3 } };
	const auto geom    = scene->AddVatGeom(verts, indices, desc, pbr);
	REQUIRE(geom.geomType == bgl::GeomType::kVat);

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 0.0f, 10.0f),
			glm::vec3(0.0f, 0.0f, 9.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	SECTION("three placements, three shapes, one geom - vat_frozen_frames.png")
	{
		const auto at = [](float x, float y) {
			return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
		};

		using VatDesc = bgl::ISceneView::VatInstanceDesc;
		view->CreateVatMeshInstance(geom, at(-3.2f, 0.0f), VatDesc{ 0, 0.0f });
		view->CreateVatMeshInstance(geom, at(0.0f, 0.0f), VatDesc{ 0, 1.0f });
		view->CreateVatMeshInstance(geom, at(3.2f, 0.0f), VatDesc{ 1, 0.0f });

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, "assets/golden/vat_frozen_frames.got.png");

		// ~52 px per world unit: the projection's focal length is 300 / tan(30 deg) = 519.6 px,
		// and the quads sit 10 units from the camera. Screen = (400 + 52x, 300 - 52y).

		// World (1.9, 0.6) of the middle instance is inside the sheared quad -- its slanted right
		// edge passes x = 2.2 at that height -- and past the flat quad's x = 1 entirely: geometry
		// here proves the middle instance fetched frame 1, not the bind pose its vertex buffer
		// holds.
		const auto shearedSpill =
			bgl::test::MeanColor("assets/golden/vat_frozen_frames.got.png", 480, 250, 20, 20);
		CHECK(shearedSpill.Luma() > 0.05f);

		// The same offset beside the frozen-at-frame-0 instance must be empty background.
		const auto flatSpill =
			bgl::test::MeanColor("assets/golden/vat_frozen_frames.got.png", 322, 259, 20, 20);
		CHECK(flatSpill.Luma() < 0.01f);

		// Clip 1's quad is half size: geometry at its centre, background where only the full quad
		// would reach -- the third instance read its own clip's rows, not clip 0.
		const auto halfCentre =
			bgl::test::MeanColor("assets/golden/vat_frozen_frames.got.png", 556, 290, 20, 20);
		CHECK(halfCentre.Luma() > 0.05f);
		const auto halfCorner =
			bgl::test::MeanColor("assets/golden/vat_frozen_frames.got.png", 598, 332, 20, 20);
		CHECK(halfCorner.Luma() < 0.01f);

		CHECK(
			bgl::test::MatchesGolden(
				"assets/golden/vat_frozen_frames.exp.png",
				"assets/golden/vat_frozen_frames.got.png"));
	}

	SECTION("a clip index past the table is refused")
	{
		CHECK_THROWS_AS(
			view->CreateVatMeshInstance(
				geom,
				glm::mat4(1.0f),
				bgl::ISceneView::VatInstanceDesc{ 2, 0.0f }),
			bgl::SceneError);
	}

	SECTION("a static instance of a VAT geom is refused, and the reverse")
	{
		CHECK_THROWS_AS(view->CreateStaticMeshInstance(geom, glm::mat4(1.0f)), bgl::SceneError);

		const auto cube = scene->AddCubeGeom(pbr);
		CHECK_THROWS_AS(
			view->CreateVatMeshInstance(
				cube,
				glm::mat4(1.0f),
				bgl::ISceneView::VatInstanceDesc{ 0, 0.0f }),
			bgl::SceneError);
	}

	SECTION("a VAT geom demands its textures, clips and an opaque PBR material")
	{
		auto broken = desc;
		broken.clips.clear();
		CHECK_THROWS_AS(scene->AddVatGeom(verts, indices, broken, pbr), bgl::SceneError);

		// The shader clamps the frame to frameCount - 1, so a zero would underflow a uint.
		auto emptyClip                = desc;
		emptyClip.clips[1].frameCount = 0;
		CHECK_THROWS_AS(scene->AddVatGeom(verts, indices, emptyClip, pbr), bgl::SceneError);

		auto noTexture      = desc;
		noTexture.positions = bgl::TextureAssetHandle{};
		CHECK_THROWS_AS(scene->AddVatGeom(verts, indices, noTexture, pbr), bgl::SceneError);

		// Deleted is as unusable as never-created: the record would bake a dead descriptor.
		auto dead    = desc;
		dead.normals = scene->AddTextureAsset(MakeNormalTexture(), "vat-doomed");
		scene->DeleteTextureAsset(dead.normals);
		CHECK_THROWS_AS(scene->AddVatGeom(verts, indices, dead, pbr), bgl::SceneError);

		CHECK_THROWS_AS(
			scene->AddVatGeom(verts, indices, desc, bgl::MaterialHandle{}),
			bgl::SceneError);

		auto cutout          = bgl::PbrMaterialDesc();
		cutout.layerType     = bgl::LayerType::kMask;
		const auto cutoutPbr = scene->CreatePbrMaterial(cutout);
		CHECK_THROWS_AS(scene->AddVatGeom(verts, indices, desc, cutoutPbr), bgl::SceneError);
	}

	SECTION("an override cannot smuggle a cutout onto a VAT instance")
	{
		const auto instance = view->CreateVatMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::ISceneView::VatInstanceDesc{ 0, 0.0f });

		auto cutout          = bgl::PbrMaterialDesc();
		cutout.layerType     = bgl::LayerType::kMask;
		const auto cutoutPbr = scene->CreatePbrMaterial(cutout);

		// Refused with an exception, never resolved: SubmeshPso(kVat, cutout) is a gfatal, so the
		// door has to close here, on the caller's thread, as an ordinary argument error.
		CHECK_THROWS_AS(view->SetSubmeshMaterialOverride(instance, 0, cutoutPbr), bgl::SceneError);
		CHECK_THROWS_AS(scene->SetSubmeshMaterial(geom, 0, cutoutPbr), bgl::SceneError);

		// An opaque PBR override is the supported skin path and must still work.
		auto skinDesc            = bgl::PbrMaterialDesc();
		skinDesc.baseColorFactor = glm::vec4(0.2f, 0.6f, 0.9f, 1.0f);
		const auto skin          = scene->CreatePbrMaterial(skinDesc);
		view->SetSubmeshMaterialOverride(instance, 0, skin);
		gfx->DrawFrame(target, job);
	}

	SECTION("deleting the instance and the geom leaves the scene clean")
	{
		const auto instance = view->CreateVatMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::ISceneView::VatInstanceDesc{ 0, 0.0f });

		gfx->DrawFrame(target, job);

		view->DeleteMeshInstance(instance);
		scene->DeleteGeom(geom);
		CHECK_FALSE(scene->IsGeomAlive(geom));

		// A frame after the teardown: nothing left referencing the freed VAT ranges may draw.
		gfx->DrawFrame(target, job);
	}
}
