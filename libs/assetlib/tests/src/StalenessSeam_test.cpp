#include <assetlib/bmaterial_io.h>
#include <assetlib/pak_io.h>
#include <assetlib_structs/BMaterial.h>
#include <core/file/LooseFileSystem.h>

#include "MountAt.h"
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
			fs::create_directories(path / "Textures");
			fs::create_directories(path / "textures_src");
		}
		~Scratch() { fs::remove_all(path); }
	};

	void
	Write(const fs::path& path, std::string_view content)
	{
		std::ofstream out(path, std::ios::binary);
		out << content;
	}

	// A material whose bake is current: its one route is stamped as it is on disk, and the triplet
	// entry it names is there to sample.
	BMaterial
	MakeBakedMaterial(const fs::path& root)
	{
		BMaterial material;
		material.name                 = "skin";
		material.pbr.routes[0]        = { "textures_src/skin.ktx2", 0 };
		material.pbr.routeStamps[0]   = stampOf(root / "textures_src/skin.ktx2");
		material.pbr.baseColorTexture = "Textures/skin_baked.ktx2";
		return material;
	}

	void
	Pack(const fs::path& root)
	{
		const core::file::LooseFileSystem loose(root);

		PakWriter writer(root / "Data.bpak");
		for (const std::string& entry : loose.Enumerate())
			writer.Add(entry, loose.Read(entry), loose.Stat(entry).value());
		writer.Finish();
	}
}

TEST_CASE("stampOf reads the same numbers through a mount as by path", "[staleseam]")
{
	const Scratch scratch("stale_seam_stamp");
	Write(scratch.path / "textures_src/skin.ktx2", "some source bytes");
	Pack(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	const SourceStamp direct = stampOf(scratch.path / "textures_src/skin.ktx2");
	REQUIRE(direct.size != 0);

	// FileStamp and SourceStamp are the same two numbers; this is the one place that says so.
	CHECK(stampOf(loose, "textures_src/skin.ktx2") == direct);
	CHECK(stampOf(pak, "textures_src/skin.ktx2") == direct);

	// A missing path is an answer, not a failure: the zeroed stamp never equals a live one, so the
	// caller reads it as stale. Propagating Stat's empty optional would make every predicate handle
	// it separately.
	SECTION("an absent path yields the zeroed stamp rather than throwing")
	{
		CHECK(stampOf(loose, "textures_src/gone.ktx2") == SourceStamp{});
		CHECK(stampOf(pak, "textures_src/gone.ktx2") == SourceStamp{});
	}
}

TEST_CASE("a material's verdict is the same from a directory and from an archive", "[staleseam]")
{
	const Scratch scratch("stale_seam_verdict");

	SECTION("a current bake reads baked through both")
	{
		Write(scratch.path / "textures_src/skin.ktx2", "some source bytes");
		Write(scratch.path / "Textures/skin_baked.ktx2", "baked bytes");
		saveMaterial(MakeBakedMaterial(scratch.path), scratch.path / "Materials/skin.bmaterial");
		Pack(scratch.path);

		const core::file::LooseFileSystem loose(scratch.path);
		const PakFile                     pak(scratch.path / "Data.bpak");
		const BMaterial material = loadMaterial(scratch.path / "Materials/skin.bmaterial");

		CHECK_FALSE(bakeIsStale(material, loose));
		CHECK_FALSE(bakeIsStale(material, pak));
		CHECK_FALSE(drawsLoose(material, loose));
		CHECK_FALSE(drawsLoose(material, pak));
	}

	SECTION("a never-baked material reads loose through both")
	{
		Write(scratch.path / "textures_src/skin.ktx2", "some source bytes");

		BMaterial material;
		material.name          = "skin";
		material.pbr.routes[0] = { "textures_src/skin.ktx2", 0 };  // stamp left zeroed
		saveMaterial(material, scratch.path / "Materials/skin.bmaterial");
		Pack(scratch.path);

		const core::file::LooseFileSystem loose(scratch.path);
		const PakFile                     pak(scratch.path / "Data.bpak");

		CHECK(bakeIsStale(material, loose));
		CHECK(bakeIsStale(material, pak));
		CHECK(drawsLoose(material, loose));
		CHECK(drawsLoose(material, pak));
	}
}

/**
 * The rule that makes a packed project safe to ship: `PakFile` reports the stamp each entry had when
 * it was packed, so an archive's verdict is frozen at pack time.
 *
 * Editing a source afterwards makes the loose tree stale -- correctly, it is -- while the archive
 * still reads current, because inside the archive nothing has moved. An archive that answered from
 * the live filesystem would flip a shipped material onto its loose routes on a machine where the
 * sources happen to sit beside it, which is a silent change of what is drawn.
 */
TEST_CASE("editing a source after packing moves the loose verdict alone", "[staleseam]")
{
	const Scratch scratch("stale_seam_frozen");

	Write(scratch.path / "textures_src/skin.ktx2", "some source bytes");
	Write(scratch.path / "Textures/skin_baked.ktx2", "baked bytes");

	const BMaterial material = MakeBakedMaterial(scratch.path);
	saveMaterial(material, scratch.path / "Materials/skin.bmaterial");
	Pack(scratch.path);

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	REQUIRE_FALSE(bakeIsStale(material, loose));
	REQUIRE_FALSE(bakeIsStale(material, pak));

	// Longer, not merely rewritten: mtime is seconds, and a same-second touch of equal length would
	// not move the stamp at all.
	Write(
		scratch.path / "textures_src/skin.ktx2",
		"some source bytes, edited and longer than before");

	CHECK(bakeIsStale(material, loose));
	CHECK_FALSE(bakeIsStale(material, pak));

	// And the draw question follows it: the loose tree falls back to routes that are still there.
	CHECK(drawsLoose(material, loose));
	CHECK_FALSE(drawsLoose(material, pak));
}

// A packed source that is gone from the loose tree is still present to the archive, which is the
// same rule seen from the other side -- an archive is self-contained.
TEST_CASE("a source deleted from the tree survives inside the archive", "[staleseam]")
{
	const Scratch scratch("stale_seam_deleted");

	Write(scratch.path / "textures_src/skin.ktx2", "some source bytes");
	Write(scratch.path / "Textures/skin_baked.ktx2", "baked bytes");

	const BMaterial material = MakeBakedMaterial(scratch.path);
	Pack(scratch.path);

	fs::remove(scratch.path / "textures_src/skin.ktx2");

	const core::file::LooseFileSystem loose(scratch.path);
	const PakFile                     pak(scratch.path / "Data.bpak");

	CHECK(bakeIsStale(material, loose));
	CHECK_FALSE(bakeIsStale(material, pak));

	// Stale and unbakeable: the source it would composite from is gone, so it keeps its triplet
	// rather than failing to open a file that is not there.
	CHECK_FALSE(drawsLoose(material, loose));
}
