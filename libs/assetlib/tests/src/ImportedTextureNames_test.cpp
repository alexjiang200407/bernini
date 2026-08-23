#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMeshImport.h>

using namespace assetlib;
using namespace assetlib::imp;

namespace
{
	// An import carrying one texture per name, which is all importedTextureFileNames reads: the
	// pixels never enter the naming rule.
	BMeshImport
	ImportNaming(std::vector<std::string> names)
	{
		auto mesh         = BMeshImport();
		mesh.textures     = std::vector<ImageData>(names.size());
		mesh.textureNames = std::move(names);
		return mesh;
	}
}

TEST_CASE("An extracted texture is named after the image it came from", "[import][textures]")
{
	const auto names = importedTextureFileNames(ImportNaming({ "albedo", "normal", "orm" }));
	CHECK(names == std::vector<std::string>{ "albedo.ktx2", "normal.ktx2", "orm.ktx2" });
}

TEST_CASE("Inserting an image moves no other texture's name", "[import][textures]")
{
	// The whole point of naming by image rather than by index. Under the old `texN` rule the
	// insertion shifted every later name by one, so a material routed at `tex1.ktx2` silently
	// began drawing what had been `tex0.ktx2`.
	const auto before = importedTextureFileNames(ImportNaming({ "albedo", "normal" }));
	const auto after  = importedTextureFileNames(ImportNaming({ "albedo", "emissive", "normal" }));

	CHECK(before[0] == after[0]);
	CHECK(before[1] == after[2]);
	CHECK(after[1] == "emissive.ktx2");
}

TEST_CASE("An image the source names nothing falls back to its index", "[import][textures]")
{
	const auto names = importedTextureFileNames(ImportNaming({ "", "normal", "" }));
	CHECK(names == std::vector<std::string>{ "tex0.ktx2", "normal.ktx2", "tex2.ktx2" });
}

TEST_CASE("A name that is not a portable file name is sanitised", "[import][textures]")
{
	const auto names = importedTextureFileNames(
		ImportNaming({ "Base Color.png", "T:\\art\\normal.tga", "rou/ghness", "..." }));

	// Every character outside [A-Za-z0-9-_] folds to one `_`, a trailing image extension goes, and
	// a name that survives as nothing at all takes the index instead.
	CHECK(names[0] == "Base_Color.ktx2");
	CHECK(names[1] == "T_art_normal.ktx2");
	CHECK(names[2] == "rou_ghness.ktx2");
	CHECK(names[3] == "tex3.ktx2");
}

TEST_CASE("A dot that is not an extension stays in the name", "[import][textures]")
{
	// Only a known image extension comes off. Stripping whatever follows the last dot would rename
	// an artist's `Body_v1.2` to `Body_v1`, and silently collide it with a `Body_v1.1` beside it.
	const auto names =
		importedTextureFileNames(ImportNaming({ "Body_v1.2", "Body_v1.1", "skin.2k", "hair.PNG" }));

	CHECK(names[0] == "Body_v1_2.ktx2");
	CHECK(names[1] == "Body_v1_1.ktx2");
	CHECK(names[2] == "skin_2k.ktx2");
	CHECK(names[3] == "hair.ktx2");
}

TEST_CASE("Two images resolving to one name get different files", "[import][textures]")
{
	// Case-insensitively, because the filesystems this writes to are: on macOS and Windows
	// `Albedo.ktx2` and `albedo.ktx2` are one file, and the second write would eat the first.
	const auto names =
		importedTextureFileNames(ImportNaming({ "albedo", "Albedo", "albedo", "albedo_2" }));

	auto distinct = std::set<std::string>();
	for (const std::string& name : names)
	{
		auto lowered = name;
		std::ranges::transform(lowered, lowered.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		CHECK(distinct.insert(lowered).second);
	}

	CHECK(names[0] == "albedo.ktx2");
}

TEST_CASE("The names come off the glTF's own images", "[import][textures][gltf]")
{
	const std::filesystem::path glb = "assets/apples.glb";
	REQUIRE(std::filesystem::exists(glb));

	const BMeshImport import = loadFromGltf(glb);
	REQUIRE(import.textures.size() == 2);

	CHECK(
		importedTextureFileNames(import) ==
		std::vector<std::string>{ "Apple1_u1_v1_diffuse.ktx2", "Apple2_u1_v1_diffuse.ktx2" });
}
