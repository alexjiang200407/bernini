#include <assetlib/codecs.h>
#include <assetlib/image_io.h>
#include <assetlib/pak.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>
#include <core/file/LayeredFileSystem.h>
#include <core/file/LooseFileSystem.h>

#include "MountAt.h"
#include "bmesh_texture.h"
#include "mounted_io.h"

using namespace assetlib;

namespace
{
	namespace fs = std::filesystem;

	struct Scratch
	{
		fs::path path;

		explicit Scratch(const char* name) : path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Materials");
			fs::create_directories(path / "Env");
			fs::create_directories(path / "Textures");
		}
		~Scratch() { fs::remove_all(path); }
	};

	BMaterial
	MakeMaterial()
	{
		BMaterial material;
		material.name                 = "brushed_metal";
		material.pbr.baseColorTexture = "Textures/albedo.ktx2";
		material.pbr.ormTexture       = "Textures/orm.ktx2";
		material.pbr.baseColorFactor  = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
		material.pbr.metallicFactor   = 0.75f;
		material.pbr.roughnessFactor  = 0.25f;
		material.pbr.routeStamps      = { SourceStamp{ 4096, 1700000000 } };
		return material;
	}

	BSky
	MakeSky()
	{
		BSky sky;
		sky.name       = "forest";
		sky.sky.source = "textures_src/forest_sky.ktx2";
		sky.sky.baked  = "Textures/sky_0123456789abcdef.ktx2";
		sky.sky.stamp  = SourceStamp{ 4096, 1700000000 };
		return sky;
	}

	BEnvLighting
	MakeLighting()
	{
		BEnvLighting lighting;
		lighting.name              = "forest";
		lighting.prefilter.source  = "textures_src/forest_prefilter.ktx2";
		lighting.prefilter.baked   = "Textures/prefilter_fedcba9876543210.ktx2";
		lighting.prefilter.stamp   = SourceStamp{ 8192, 1700000001 };
		lighting.irradiance.source = "textures_src/forest_irradiance.ktx2";
		lighting.irradiance.baked  = "Textures/irradiance_00ff00ff00ff00ff.ktx2";
		lighting.irradiance.stamp  = SourceStamp{ 1024, 1700000002 };
		lighting.exposure          = 0.375f;
		return lighting;
	}

	BEnv
	MakeEnv()
	{
		BEnv env;
		env.name     = "forest";
		env.sky      = "Env/forest.bsky";
		env.lighting = "Env/forest.benvl";
		return env;
	}

	// A gradient rather than a flat fill: a transcode that dropped or reordered a mip still produces
	// the right byte count, and only differing texels catch it.
	ImageData
	MakeTexture(uint32_t width, uint32_t height)
	{
		std::vector<std::byte> rgba(static_cast<size_t>(width) * height * 4);
		for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<std::byte>((i * 7) & 0xFF);

		return rgba8ToImage(rgba, width, height);
	}

	bool
	SamePixels(const ImageData& a, const ImageData& b)
	{
		return a.pixels.size() == b.pixels.size() &&
		       std::memcmp(a.pixels.data(), b.pixels.data(), a.pixels.size()) == 0;
	}

	// Everything ImageData carries, so a decode that differed in any of it fails here rather than in
	// whichever consumer happened to read that field.
	void
	CheckSameImage(const ImageData& mounted, const ImageData& direct)
	{
		CHECK(mounted.width == direct.width);
		CHECK(mounted.height == direct.height);
		CHECK(mounted.mipLevels == direct.mipLevels);
		CHECK(mounted.arraySize == direct.arraySize);
		CHECK(mounted.isCubemap == direct.isCubemap);
		CHECK(mounted.vkFormat == direct.vkFormat);

		REQUIRE(mounted.subresources.size() == direct.subresources.size());
		for (size_t i = 0; i < direct.subresources.size(); ++i)
		{
			CHECK(mounted.subresources[i].offset == direct.subresources[i].offset);
			CHECK(mounted.subresources[i].rowPitch == direct.subresources[i].rowPitch);
			CHECK(mounted.subresources[i].slicePitch == direct.subresources[i].slicePitch);
		}

		CHECK(SamePixels(mounted, direct));
	}

	// The four flat containers and two textures, loose in `root` and packed into `root/Data.bpak`.
	void
	Stage(const fs::path& root)
	{
		SaveAt(MakeMaterial(), root / "Materials/metal.bmaterial");
		SaveAt(MakeSky(), root / "Env/forest.bsky");
		SaveAt(MakeLighting(), root / "Env/forest.benvl");
		SaveAt(MakeEnv(), root / "Env/forest.benv");

		// Basis: the transcoding path, which is where a decode is most likely to diverge.
		writeKTX2(MakeTexture(64, 64), root / "Textures/albedo.ktx2", /*srgb*/ true);
		writeKTX2(
			MakeTexture(64, 64),
			root / "Textures/orm.ktx2",
			/*srgb*/ false,
			Ktx2Compression::kNone);

		const core::file::LooseFileSystem loose(root);

		PakWriter writer(root / "Data.bpak");
		for (const std::string& entry : loose.Enumerate())
			writer.Add(entry, loose.Read(entry), loose.Stat(entry).value());
		writer.Finish();
	}
}

TEST_CASE("a flat container loads the same from a directory and from an archive", "[flatseam]")
{
	const Scratch scratch("flat_seam_equal");
	Stage(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	const std::array<const core::file::IFileSystem*, 2> mounts = { &loose, &pak };

	SECTION(".bmaterial")
	{
		const BMaterial direct = StoreAt(scratch.path).Load<BMaterial>("Materials/metal.bmaterial");

		for (const core::file::IFileSystem* mount : mounts)
		{
			const BMaterial mounted = load<BMaterial>(*mount, "Materials/metal.bmaterial");

			CHECK(mounted.name == direct.name);
			CHECK(mounted.pbr.baseColorTexture == direct.pbr.baseColorTexture);
			CHECK(mounted.pbr.routeStamps == direct.pbr.routeStamps);
			CHECK(
				AssetCodec<BMaterial>::Serialize(mounted) ==
				AssetCodec<BMaterial>::Serialize(direct));
		}
	}

	SECTION(".bsky")
	{
		const BSky direct = StoreAt(scratch.path).Load<BSky>("Env/forest.bsky");

		for (const core::file::IFileSystem* mount : mounts)
		{
			const BSky mounted = load<BSky>(*mount, "Env/forest.bsky");

			CHECK(mounted.name == direct.name);
			CHECK(mounted.sky == direct.sky);
			CHECK(AssetCodec<BSky>::Serialize(mounted) == AssetCodec<BSky>::Serialize(direct));
		}
	}

	SECTION(".benvl")
	{
		const BEnvLighting direct = StoreAt(scratch.path).Load<BEnvLighting>("Env/forest.benvl");

		for (const core::file::IFileSystem* mount : mounts)
		{
			const BEnvLighting mounted = load<BEnvLighting>(*mount, "Env/forest.benvl");

			CHECK(mounted.prefilter == direct.prefilter);
			CHECK(mounted.irradiance == direct.irradiance);
			CHECK(
				AssetCodec<BEnvLighting>::Serialize(mounted) ==
				AssetCodec<BEnvLighting>::Serialize(direct));
		}
	}

	SECTION(".benv")
	{
		const BEnv direct = StoreAt(scratch.path).Load<BEnv>("Env/forest.benv");

		for (const core::file::IFileSystem* mount : mounts)
		{
			const BEnv mounted = load<BEnv>(*mount, "Env/forest.benv");

			CHECK(mounted.name == direct.name);
			CHECK(mounted.sky == direct.sky);
			CHECK(mounted.lighting == direct.lighting);
			CHECK(AssetCodec<BEnv>::Serialize(mounted) == AssetCodec<BEnv>::Serialize(direct));
		}
	}
}

// The gate for this task: a texture is the one asset whose mounted load goes through a different
// libktx entry point (CreateFromMemory rather than CreateFromNamedFile), so "the same bytes reach
// the decoder" is a claim about two code paths and not one.
TEST_CASE("a .ktx2 decodes the same from a directory and from an archive", "[flatseam][ktx2]")
{
	const Scratch scratch("flat_seam_ktx2");
	Stage(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	const std::array<const core::file::IFileSystem*, 2> mounts = { &loose, &pak };

	SECTION("a Basis payload transcodes identically")
	{
		const ImageData direct = loadKTX2(scratch.path / "Textures/albedo.ktx2");
		REQUIRE(direct.vkFormat == VkFormat::BC7_SRGB_BLOCK);
		REQUIRE(direct.mipLevels == 7);

		for (const core::file::IFileSystem* mount : mounts)
			CheckSameImage(loadKTX2(*mount, "Textures/albedo.ktx2"), direct);
	}

	SECTION("an uncompressed texture is stored verbatim either way")
	{
		const ImageData direct = loadKTX2(scratch.path / "Textures/orm.ktx2");
		REQUIRE(direct.vkFormat == VkFormat::R8G8B8A8_UNORM);

		for (const core::file::IFileSystem* mount : mounts)
			CheckSameImage(loadKTX2(*mount, "Textures/orm.ktx2"), direct);
	}

	SECTION("the RGBA8 decode a material bake asks for")
	{
		const ImageData direct =
			loadKTX2(scratch.path / "Textures/albedo.ktx2", Ktx2Decode::kRgba8);
		REQUIRE(direct.vkFormat == VkFormat::R8G8B8A8_SRGB);

		for (const core::file::IFileSystem* mount : mounts)
			CheckSameImage(loadKTX2(*mount, "Textures/albedo.ktx2", Ktx2Decode::kRgba8), direct);
	}

	// maxDim selects among stored mips, so a mounted load that lost a level would return a smaller
	// top mip rather than fail -- a silently lower-resolution thumbnail.
	SECTION("maxDim selects the same mip tail")
	{
		const ImageData direct =
			loadKTX2(scratch.path / "Textures/albedo.ktx2", Ktx2Decode::kGpu, 16);
		REQUIRE(direct.width < 64);

		for (const core::file::IFileSystem* mount : mounts)
			CheckSameImage(loadKTX2(*mount, "Textures/albedo.ktx2", Ktx2Decode::kGpu, 16), direct);
	}

	SECTION("the preview decode")
	{
		const ImageData direct = loadKTX2Preview(scratch.path / "Textures/albedo.ktx2", 32);
		REQUIRE(direct.mipLevels == 1);

		for (const core::file::IFileSystem* mount : mounts)
			CheckSameImage(loadKTX2Preview(*mount, "Textures/albedo.ktx2", 32), direct);
	}
}

TEST_CASE("a mounted load of an absent entry throws", "[flatseam]")
{
	const Scratch scratch("flat_seam_absent");
	Stage(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	for (const core::file::IFileSystem* mount :
	     { static_cast<const core::file::IFileSystem*>(&loose),
	       static_cast<const core::file::IFileSystem*>(&pak) })
	{
		CHECK_THROWS_AS(load<BMaterial>(*mount, "Materials/gone.bmaterial"), std::runtime_error);
		CHECK_THROWS_AS(load<BSky>(*mount, "Env/gone.bsky"), std::runtime_error);
		CHECK_THROWS_AS(load<BEnvLighting>(*mount, "Env/gone.benvl"), std::runtime_error);
		CHECK_THROWS_AS(load<BEnv>(*mount, "Env/gone.benv"), std::runtime_error);
		CHECK_THROWS_AS(loadKTX2(*mount, "Textures/gone.ktx2"), std::runtime_error);
		CHECK_THROWS_AS(loadKTX2Preview(*mount, "Textures/gone.ktx2"), std::runtime_error);
	}
}

// A loose file shadowing a packed one is the overlay the editor writes. The flat containers have no
// chunk table to disagree about, so the only thing that decides the answer is which mount replies.
TEST_CASE("a loose entry shadows its packed twin", "[flatseam]")
{
	const Scratch scratch("flat_seam_shadow");
	Stage(scratch.path);

	BMaterial edited = MakeMaterial();
	edited.name      = "edited_after_packing";
	StoreAt(scratch.path).Save(edited, "Materials/metal.bmaterial");

	core::file::LayeredFileSystem mount;
	mount.Mount(std::make_shared<core::file::LooseFileSystem>(scratch.path));
	mount.Mount(std::make_shared<PakFile>(scratch.path / "Data.bpak"));

	CHECK(load<BMaterial>(mount, "Materials/metal.bmaterial").name == "edited_after_packing");
}
