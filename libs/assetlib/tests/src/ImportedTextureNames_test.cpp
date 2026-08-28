#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib_structs/BMeshImport.h>

using namespace assetlib;
using namespace assetlib::imp;

namespace
{
	// An import carrying one texture per name. `marks` stands in for the images' content, which an
	// unnamed one is named after: parallel to `names`, and defaulted to the position where a test
	// does not care. A named image ignores it.
	BMeshImport
	ImportNaming(std::vector<std::string> names, std::vector<uint32_t> marks = {})
	{
		auto mesh     = BMeshImport();
		mesh.textures = std::vector<ImageData>(names.size());
		for (size_t i = 0; i < mesh.textures.size(); ++i)
			mesh.textures[i].width = i < marks.size() ? marks[i] : static_cast<uint32_t>(i + 1);

		mesh.textureNames = std::move(names);
		return mesh;
	}

	// `tex_` and sixteen hex digits.
	bool
	IsUnnamedStem(std::string_view fileName)
	{
		if (!fileName.starts_with("tex_") || !fileName.ends_with(".ktx2"))
			return false;

		const std::string_view digits = fileName.substr(4, fileName.size() - 4 - 5);
		return digits.size() == 16 && std::ranges::all_of(digits, [](char c) {
				   return std::isxdigit(static_cast<unsigned char>(c)) != 0;
			   });
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

TEST_CASE("An image the source names nothing falls back to its content", "[import][textures]")
{
	const auto names = importedTextureFileNames(ImportNaming({ "", "normal", "" }));

	CHECK(IsUnnamedStem(names[0]));
	CHECK(names[1] == "normal.ktx2");
	CHECK(IsUnnamedStem(names[2]));
	CHECK(names[0] != names[2]);
}

TEST_CASE("Inserting an unnamed image moves no other texture's name", "[import][textures]")
{
	// The counterpart of the case above for images the source names. Under the positional rule the
	// insertion renamed every later unnamed image, so a material routed at `tex0.ktx2` silently
	// began drawing what had just been inserted -- the one corruption naming exists to prevent, left
	// in place for exactly the sources that cannot be named around it.
	const auto before = importedTextureFileNames(ImportNaming({ "", "normal" }, { 7, 9 }));
	const auto after  = importedTextureFileNames(ImportNaming({ "", "", "normal" }, { 5, 7, 9 }));

	CHECK(before[0] == after[1]);
	CHECK(before[1] == after[2]);
	CHECK(after[0] != after[1]);
}

TEST_CASE("Two unnamed images of one picture resolve to one name", "[import][textures]")
{
	// Content is the whole name, so a source carrying the same picture twice would write it twice
	// to one path. The collision suffix keeps them separate files, as it does for repeated names.
	const auto names = importedTextureFileNames(ImportNaming({ "", "" }, { 4, 4 }));
	CHECK(names[0] != names[1]);
}

TEST_CASE("A name that is not a portable file name is sanitised", "[import][textures]")
{
	const auto names = importedTextureFileNames(
		ImportNaming({ "Base Color.png", "T:\\art\\normal.tga", "rou/ghness", "..." }));

	// Every character outside [A-Za-z0-9-_] folds to one `_`, a trailing image extension goes, and
	// a name that survives as nothing at all is an unnamed image.
	CHECK(names[0] == "Base_Color.ktx2");
	CHECK(names[1] == "T_art_normal.ktx2");
	CHECK(names[2] == "rou_ghness.ktx2");
	CHECK(IsUnnamedStem(names[3]));
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
