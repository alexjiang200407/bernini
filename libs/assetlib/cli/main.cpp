#include <CLI/CLI.hpp>
#include <assetlib/asset_describe.h>
#include <assetlib/assetlib.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <spdlog/spdlog.h>

namespace
{
	enum class AssetKind
	{
		kMesh,
		kMaterial,
	};

	// Both containers open with a 4-byte magic, so the kind is read from the file rather than guessed
	// from its extension -- `describe` then works on a file named anything.
	AssetKind
	sniff(const std::filesystem::path& path)
	{
		constexpr uint32_t c_MeshMagic     = 0x48534D42u;  // 'BMSH'
		constexpr uint32_t c_MaterialMagic = 0x54414D42u;  // 'BMAT'

		std::ifstream in(path, std::ios::binary);
		uint32_t      magic = 0;
		if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)))
			throw std::runtime_error("cannot read the file header of " + path.string());

		if (magic == c_MeshMagic)
			return AssetKind::kMesh;
		if (magic == c_MaterialMagic)
			return AssetKind::kMaterial;

		throw std::runtime_error(
			path.string() + " is neither a .bmesh nor a .bmaterial (unrecognized magic)");
	}
}

int
main(int argc, char** argv)
{
	CLI::App app{ "Bernini asset pipeline CLI" };
	app.set_version_flag("--version", assetlib::Version());
	app.require_subcommand(1);

	std::string input;
	std::string outDir;
	std::string name = "mesh";

	auto* bake = app.add_subcommand(
		"bake",
		"Convert a glTF (.glb/.gltf) into a modular .bmesh + .bmaterial + .ktx2 texture set");
	bake->add_option("input", input, "Source .glb/.gltf file")
		->required()
		->check(CLI::ExistingFile);
	bake->add_option("-o,--out", outDir, "Output directory")->required();
	bake->add_option("-n,--name", name, "Base name for the .bmesh (default: mesh)");

	std::string objInput;
	std::string objOut;
	bool        objRaw = false;

	auto* obj = app.add_subcommand("obj", "Dump a .bmesh as a Wavefront .obj for inspection");
	obj->add_option("input", objInput, "Source .bmesh file")->required()->check(CLI::ExistingFile);
	obj->add_option("-o,--out", objOut, "Output .obj file")->required();
	obj->add_flag(
		"--raw",
		objRaw,
		"Emit the raw index buffer instead of the meshlet-reconstructed geometry");

	std::string describeInput;
	std::string describeDataRoot;
	bool        describeBrief = false;

	auto* describe =
		app.add_subcommand("describe", "Print the contents of a .bmesh or .bmaterial as text");
	describe->add_option("input", describeInput, "Source .bmesh or .bmaterial file")
		->required()
		->check(CLI::ExistingFile);
	describe->add_option(
		"-d,--data-root",
		describeDataRoot,
		"Project data directory the asset's paths resolve against. For a material this also stats "
		"each routed source, so a stale bake is reported");
	describe->add_flag(
		"-b,--brief",
		describeBrief,
		"Mesh only: print the summary and material table, but not every submesh");

	CLI11_PARSE(app, argc, argv);

	if (*bake)
	{
		try
		{
			const auto imported = assetlib::loadFromGltf(input);
			assetlib::bake(imported, outDir, name);
			spdlog::info(
				"Baked '{}' -> {}/{}.bmesh ({} materials, {} textures)",
				input,
				outDir,
				name,
				imported.materials.size(),
				imported.textures.size());
		}
		catch (const std::exception& e)
		{
			spdlog::error("bake failed: {}", e.what());
			return 1;
		}
	}

	if (*obj)
	{
		try
		{
			const auto mesh = assetlib::load(objInput);
			assetlib::writeObj(mesh, objOut, !objRaw);
			spdlog::info(
				"Wrote '{}' from '{}' ({} submeshes, {} source)",
				objOut,
				objInput,
				mesh.submeshes.size(),
				objRaw ? "raw-index" : "meshlet");
		}
		catch (const std::exception& e)
		{
			spdlog::error("obj dump failed: {}", e.what());
			return 1;
		}
	}

	if (*describe)
	{
		try
		{
			const std::filesystem::path path(describeInput);

			// Straight to stdout, not the logger: this is the command's output, so it should pipe into
			// a file or a diff without spdlog's timestamps and level prefixes in the way.
			if (sniff(path) == AssetKind::kMesh)
				std::cout << assetlib::describe(assetlib::load(path), !describeBrief);
			else
				std::cout << assetlib::describe(
					assetlib::loadMaterial(path),
					std::filesystem::path(describeDataRoot));
		}
		catch (const std::exception& e)
		{
			spdlog::error("describe failed: {}", e.what());
			return 1;
		}
	}

	return 0;
}
