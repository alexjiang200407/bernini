#include <assetlib/bmaterial_io.h>
#include <assetlib/image_io.h>
#include <bgl/IGraphics.h>
#include <bgl/MaterialType.h>
#include <gamelib/AssetFactory.h>

namespace
{
	bgl::GraphicsOptions
	HeadlessOptions()
	{
		auto opts             = bgl::GraphicsOptions();
		opts.enableDebugLayer = false;
		return opts;
	}

	bgl::SceneDesc
	MaterialSceneDesc()
	{
		auto desc                    = bgl::SceneDesc();
		desc.maxGeom                 = 2;
		desc.maxSubmeshes            = 2;
		desc.maxMeshlets             = 16;
		desc.maxVertexBufferByteSize = 4096;
		desc.maxIndices              = 64;
		desc.maxPbrMaterials         = 8;
		desc.maxLoosePbrMaterials    = 8;
		return desc;
	}

	// A scratch data root that cleans up after itself.
	struct DataRoot
	{
		std::filesystem::path path;

		explicit DataRoot(const char* name) : path(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(path);
			std::filesystem::create_directories(path);
		}
		~DataRoot() { std::filesystem::remove_all(path); }
	};

	// A 1x1 uncompressed .ktx2, so the factory has something real to upload.
	void
	WriteTexture(const std::filesystem::path& path)
	{
		auto image      = assetlib::ImageData();
		image.width     = 1;
		image.height    = 1;
		image.mipLevels = 1;
		image.arraySize = 1;
		image.vkFormat  = assetlib::VkFormat::R8G8B8A8_UNORM;

		image.pixels = core::fixed_buffer<std::byte>(4);
		std::fill_n(image.pixels.data(), 4, std::byte{ 0xFF });
		image.subresources.push_back({ 0, 4, 4 });

		std::filesystem::create_directories(path.parent_path());
		assetlib::writeKTX2(image, path, false, assetlib::Ktx2Compression::kNone);
	}
}

TEST_CASE("AssetFactory creates the material its mode describes", "[gamelib][assets]")
{
	// The bug this exists to prevent: a loose material silently rendering untextured because a caller
	// read the empty triplet. `mode` says which representation to draw from, and both may be populated.
	const DataRoot root("bernini_assetfactory_mode");
	WriteTexture(root.path / "Textures" / "basecolor.ktx2");
	WriteTexture(root.path / "textures_src" / "albedo.ktx2");

	auto gfx = bgl::CreateGraphics(HeadlessOptions());
	REQUIRE(gfx != nullptr);

	auto sceneHandle = gfx->CreateScene(MaterialSceneDesc());
	REQUIRE(sceneHandle != nullptr);

	auto factory = game::AssetFactory(*sceneHandle, root.path);

	auto material             = assetlib::BMaterial();
	material.baseColorTexture = "Textures/basecolor.ktx2";
	material.routes[0]        = { "textures_src/albedo.ktx2", 0 };

	SECTION("kBaked draws from the optimized triplet")
	{
		material.mode = assetlib::MaterialMode::kBaked;

		const bgl::MaterialHandle handle = factory.CreateMaterial(material);

		REQUIRE(handle.IsValid());
		CHECK(handle.materialType == bgl::MaterialType::kPBR);
	}

	SECTION("kLoose draws from the per-channel routes")
	{
		material.mode = assetlib::MaterialMode::kLoose;

		const bgl::MaterialHandle handle = factory.CreateMaterial(material);

		REQUIRE(handle.IsValid());
		CHECK(handle.materialType == bgl::MaterialType::kLoosePbr);
	}
}

TEST_CASE("AssetFactory gives one handle per path", "[gamelib][assets]")
{
	// Identity is the reason the factory exists: a material's nine routes commonly name the same file,
	// baked maps are deliberately shared between materials, and submeshes share `.bmaterial`s.
	const DataRoot root("bernini_assetfactory_identity");
	WriteTexture(root.path / "Textures" / "shared.ktx2");

	auto gfx         = bgl::CreateGraphics(HeadlessOptions());
	auto sceneHandle = gfx->CreateScene(MaterialSceneDesc());
	REQUIRE(sceneHandle != nullptr);

	auto factory = game::AssetFactory(*sceneHandle, root.path);

	SECTION("a texture asked for twice is uploaded once")
	{
		const auto first  = factory.LoadTexture("Textures/shared.ktx2");
		const auto second = factory.LoadTexture("Textures/shared.ktx2");

		REQUIRE(first.textureSlot);
		CHECK(first.textureSlot == second.textureSlot);
	}

	SECTION("an empty path is absent, not an error")
	{
		// The scene substitutes white / a flat normal for an unset map.
		CHECK_FALSE(factory.LoadTexture("").textureSlot);
	}

	SECTION("a material asked for twice is created once")
	{
		auto material             = assetlib::BMaterial();
		material.mode             = assetlib::MaterialMode::kBaked;
		material.baseColorTexture = "Textures/shared.ktx2";
		assetlib::saveMaterial(material, root.path / "mat0.bmaterial");

		const bgl::MaterialHandle first  = factory.LoadMaterial("mat0.bmaterial");
		const bgl::MaterialHandle second = factory.LoadMaterial("mat0.bmaterial");

		REQUIRE(first.IsValid());
		CHECK(first.handle.index == second.handle.index);
	}

	SECTION("a missing texture is an error, not a silent default")
	{
		CHECK_THROWS_AS(factory.LoadTexture("Textures/absent.ktx2"), std::runtime_error);
	}
}

TEST_CASE("AssetFactory resolves paths against its data root", "[gamelib][assets]")
{
	// Every asset reference Bernini stores is relative to the Data directory, supplied once here.
	const DataRoot root("bernini_assetfactory_root");
	WriteTexture(root.path / "nested" / "deep" / "tex.ktx2");

	auto gfx         = bgl::CreateGraphics(HeadlessOptions());
	auto sceneHandle = gfx->CreateScene(MaterialSceneDesc());

	auto factory = game::AssetFactory(*sceneHandle, root.path);

	CHECK(factory.DataRoot() == root.path);
	CHECK(factory.LoadTexture("nested/deep/tex.ktx2").textureSlot);
}
