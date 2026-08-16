#include "gfx/GraphicsBase.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VatSynth.h"
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

	using bgl::test::vat_synth::MakeFlatNormalTexture;
	using bgl::test::vat_synth::MakeImage;

	void
	WritePositionRow(assetlib::ImageData& image, uint32_t row, std::span<const glm::vec3> corners)
	{
		bgl::test::vat_synth::WritePositionRow(image, row, corners, c_BoundsMin, c_BoundsMax);
	}

	assetlib::ImageData
	MakePositionTexture()
	{
		auto image = MakeImage(c_Columns, c_Rows, assetlib::VkFormat::R16G16B16A16_UNORM, 8);

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
		return MakeFlatNormalTexture(c_Columns, c_Rows);
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

	// --- The AddVatMeshGeom fixture: a two-submesh BMesh whose triangles live only in the texture. ---

	// Each submesh is one CCW triangle; the texture places submesh 0's at x ~ -1.25 and submesh 1's
	// at x ~ +1.25. The vertex *bytes* hold no positions worth reading -- the fetch is what is
	// under test, so all signal is in the texture and the column bases.
	const std::array<glm::vec3, 6> c_TwoTriangles = { {
		{ -2.0f, -1.0f, 0.0f },
		{ -0.5f, -1.0f, 0.0f },
		{ -1.25f, 1.0f, 0.0f },
		{ 0.5f, -1.0f, 0.0f },
		{ 2.0f, -1.0f, 0.0f },
		{ 1.25f, 1.0f, 0.0f },
	} };

	const glm::vec3 c_TriBoundsMin(-2.0f, -1.5f, -1.0f);
	const glm::vec3 c_TriBoundsMax(2.0f, 1.5f, 1.0f);

	constexpr uint32_t c_TriColumns = 6;
	constexpr uint32_t c_TriRows    = 2;  // frame 0 and its pad

	assetlib::ImageData
	MakeTriPositionTexture()
	{
		auto image = MakeImage(c_TriColumns, c_TriRows, assetlib::VkFormat::R16G16B16A16_UNORM, 8);
		for (uint32_t row = 0; row < c_TriRows; ++row)
		{
			bgl::test::vat_synth::WritePositionRow(
				image,
				row,
				c_TwoTriangles,
				c_TriBoundsMin,
				c_TriBoundsMax);
		}
		return image;
	}

	assetlib::ImageData
	MakeTriNormalTexture()
	{
		return MakeFlatNormalTexture(c_TriColumns, c_TriRows);
	}

	// Two submeshes of one triangle each, materials 0 and 1. Vertex data is position-only zeros:
	// a VAT draw never reads it, and the culling sphere comes from the desc's box.
	assetlib::BMesh
	MakeTwoSubmeshMesh()
	{
		constexpr uint16_t c_Stride = 12;

		auto mesh = assetlib::BMesh();
		mesh.vertexData.resize(size_t(6) * c_Stride);

		for (uint32_t s = 0; s < 2; ++s)
		{
			auto meshlet           = assetlib::Meshlet();
			meshlet.vertexOffset   = static_cast<uint32_t>(mesh.meshletVertices.size());
			meshlet.triangleOffset = static_cast<uint32_t>(mesh.meshletTriangles.size());
			meshlet.vertexCount    = 3;
			meshlet.triangleCount  = 1;
			meshlet.boundingCenter = glm::vec3(0.0f);
			meshlet.boundingRadius = 3.0f;
			mesh.meshlets.push_back(meshlet);

			for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
			for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

			auto submesh                  = assetlib::Submesh();
			submesh.layout.attributeCount = 1;
			submesh.layout.stride         = c_Stride;
			submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
				                              assetlib::VertexFormat::kFloat32x3,
				                              0 };
			submesh.vertexByteOffset      = s * 3 * c_Stride;
			submesh.vertexCount           = 3;
			submesh.firstMeshlet          = s;
			submesh.meshletCount          = 1;
			submesh.material              = s;
			submesh.aabbMin               = glm::vec3(-2.0f);
			submesh.aabbMax               = glm::vec3(2.0f);
			submesh.nameOffset            = 0;
			mesh.submeshes.push_back(submesh);
		}

		auto entry         = assetlib::Mesh();
		entry.firstSubmesh = 0;
		entry.submeshCount = 2;
		entry.nameOffset   = 0;
		mesh.meshes.push_back(entry);

		return mesh;
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
	const auto geom    = scene->AddVatMeshGeom(verts, indices, desc, pbr);
	REQUIRE(geom.geomType == bgl::GeomType::kVatMesh);

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
		CHECK_THROWS_AS(scene->AddVatMeshGeom(verts, indices, broken, pbr), bgl::SceneError);

		// The shader clamps the frame to frameCount - 1, so a zero would underflow a uint.
		auto emptyClip                = desc;
		emptyClip.clips[1].frameCount = 0;
		CHECK_THROWS_AS(scene->AddVatMeshGeom(verts, indices, emptyClip, pbr), bgl::SceneError);

		auto noTexture      = desc;
		noTexture.positions = bgl::TextureAssetHandle{};
		CHECK_THROWS_AS(scene->AddVatMeshGeom(verts, indices, noTexture, pbr), bgl::SceneError);

		// Deleted is as unusable as never-created: the record would bake a dead descriptor.
		auto dead    = desc;
		dead.normals = scene->AddTextureAsset(MakeNormalTexture(), "vat-doomed");
		scene->DeleteTextureAsset(dead.normals);
		CHECK_THROWS_AS(scene->AddVatMeshGeom(verts, indices, dead, pbr), bgl::SceneError);

		CHECK_THROWS_AS(
			scene->AddVatMeshGeom(verts, indices, desc, bgl::MaterialHandle{}),
			bgl::SceneError);

		auto cutout          = bgl::PbrMaterialDesc();
		cutout.layerType     = bgl::LayerType::kMask;
		const auto cutoutPbr = scene->CreatePbrMaterial(cutout);
		CHECK_THROWS_AS(scene->AddVatMeshGeom(verts, indices, desc, cutoutPbr), bgl::SceneError);
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

		// Refused with an exception, never resolved: SubmeshPso(kVatMesh, cutout) is a gfatal, so the
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

TEST_CASE("A VAT mesh's submeshes read their own columns", "[vat][render]")
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

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const auto positions = scene->AddTextureAsset(MakeTriPositionTexture(), "vat-tri-positions");
	const auto normals   = scene->AddTextureAsset(MakeTriNormalTexture(), "vat-tri-normals");

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
	const auto pbrA          = scene->CreatePbrMaterial(material);
	material.baseColorFactor = glm::vec4(0.2f, 0.6f, 0.9f, 1.0f);
	const auto pbrB          = scene->CreatePbrMaterial(material);
	const auto materials     = std::array<bgl::MaterialHandle, 2>{ { pbrA, pbrB } };

	auto desc      = bgl::VatGeomDesc();
	desc.positions = positions;
	desc.normals   = normals;
	desc.boundsMin = c_TriBoundsMin;
	desc.boundsMax = c_TriBoundsMax;
	desc.clips     = { { 0, 1, 30.0f, false } };

	const auto mesh = MakeTwoSubmeshMesh();

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

	// ~52 px per world unit, centre (400, 300): the triangles' lower halves straddle
	// world (-1.25, -0.5) and (+1.25, -0.5).
	const auto probe = [](const char* png, float worldX) {
		const int px = static_cast<int>(std::lround(400.0f + 51.96f * worldX));
		return bgl::test::MeanColor(png, px - 6, 320, 12, 12).Luma();
	};

	SECTION("each submesh fetches from its own column base")
	{
		desc.columnBases = { 0, 3 };
		const auto geom  = scene->AddVatMeshGeom(mesh, 0, materials, desc);
		REQUIRE(geom.geomType == bgl::GeomType::kVatMesh);

		view->CreateVatMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::ISceneView::VatInstanceDesc{ 0, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_mesh_columns.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(probe(png, -1.25f) > 0.05f);
		CHECK(probe(png, 1.25f) > 0.05f);
		CHECK(probe(png, 0.0f) < 0.01f);
	}

	SECTION("the bases are consumed, not assumed: base 0 twice draws both triangles left")
	{
		desc.columnBases = { 0, 0 };
		const auto geom  = scene->AddVatMeshGeom(mesh, 0, materials, desc);

		view->CreateVatMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::ISceneView::VatInstanceDesc{ 0, 0.0f });

		gfx->DrawFrame(target, job);
		const auto* png = "assets/golden/vat_mesh_columns_zero.got.png";
		gfx->ScreenshotPng(target, png);

		CHECK(probe(png, -1.25f) > 0.05f);
		CHECK(probe(png, 1.25f) < 0.01f);
	}

	SECTION("a columnBases count that does not match the submeshes is refused")
	{
		desc.columnBases = { 0 };
		CHECK_THROWS_AS(scene->AddVatMeshGeom(mesh, 0, materials, desc), bgl::SceneError);

		desc.columnBases.clear();
		CHECK_THROWS_AS(scene->AddVatMeshGeom(mesh, 0, materials, desc), bgl::SceneError);
	}

	SECTION("a submesh without an opaque PBR material is refused")
	{
		desc.columnBases = { 0, 3 };

		auto cutout          = bgl::PbrMaterialDesc();
		cutout.layerType     = bgl::LayerType::kMask;
		const auto cutoutPbr = scene->CreatePbrMaterial(cutout);

		const auto mixed = std::array<bgl::MaterialHandle, 2>{ { pbrA, cutoutPbr } };
		CHECK_THROWS_AS(scene->AddVatMeshGeom(mesh, 0, mixed, desc), bgl::SceneError);

		const auto missing = std::array<bgl::MaterialHandle, 1>{ { pbrA } };
		CHECK_THROWS_AS(scene->AddVatMeshGeom(mesh, 0, missing, desc), bgl::SceneError);

		CHECK_THROWS_AS(scene->AddVatMeshGeom(mesh, 1, materials, desc), bgl::SceneError);
	}
}
