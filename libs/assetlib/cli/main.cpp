#include <CLI/CLI.hpp>
#include <assetlib/asset_describe.h>
#include <assetlib/asset_refs.h>
#include <assetlib/assetlib.h>
#include <assetlib/banim_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/texture_prune.h>
#include <spdlog/spdlog.h>

namespace
{
	enum class ContainerType
	{
		kMesh,
		kMaterial,
		kSkeleton,
		kAnimation,
	};

	std::string
	formatBytes(uint64_t bytes)
	{
		constexpr std::array<const char*, 4> c_Units = { { "B", "KiB", "MiB", "GiB" } };

		auto   value = static_cast<double>(bytes);
		size_t unit  = 0;
		while (value >= 1024.0 && unit + 1 < c_Units.size())
		{
			value /= 1024.0;
			++unit;
		}

		char text[32] = {};
		std::snprintf(text, sizeof(text), unit == 0 ? "%.0f %s" : "%.1f %s", value, c_Units[unit]);
		return text;
	}

	// Reads a yes/no answer from stdin. A closed or piped-empty stdin answers no: the safe direction
	// for a destructive command that nobody is there to confirm.
	bool
	confirm(const std::string& question)
	{
		std::cout << question << " [y/N] " << std::flush;

		std::string answer;
		if (!std::getline(std::cin, answer))
			return false;

		return answer == "y" || answer == "Y" || answer == "yes";
	}

	std::string_view
	describeRefKind(assetlib::RefKind kind)
	{
		switch (kind)
		{
		case assetlib::RefKind::kSubmeshMaterial:
			return "names, as a submesh material,";
		case assetlib::RefKind::kBakedMap:
			return "baked";
		case assetlib::RefKind::kChannelRoute:
			return "routes a channel from";
		case assetlib::RefKind::kMeshSkeleton:
			return "skins to";
		case assetlib::RefKind::kClipSkeleton:
			return "was resampled against";
		}

		return "references";
	}

	// Every container opens with a 4-byte magic, so the type is read from the file rather than guessed
	// from its extension -- `describe` then works on a file named anything. This is the deliberate
	// opposite of assetlib::assetTypeFromExtension, which never opens the file.
	ContainerType
	sniff(const std::filesystem::path& path)
	{
		constexpr uint32_t c_MeshMagic      = 0x48534D42u;  // 'BMSH'
		constexpr uint32_t c_MaterialMagic  = 0x54414D42u;  // 'BMAT'
		constexpr uint32_t c_SkeletonMagic  = 0x4C4B5342u;  // 'BSKL'
		constexpr uint32_t c_AnimationMagic = 0x4D4E4142u;  // 'BANM'

		std::ifstream in(path, std::ios::binary);
		uint32_t      magic = 0;
		if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)))
			throw std::runtime_error("cannot read the file header of " + path.string());

		if (magic == c_MeshMagic)
			return ContainerType::kMesh;
		if (magic == c_MaterialMagic)
			return ContainerType::kMaterial;
		if (magic == c_SkeletonMagic)
			return ContainerType::kSkeleton;
		if (magic == c_AnimationMagic)
			return ContainerType::kAnimation;

		throw std::runtime_error(
			path.string() + " is not a .bmesh, .bmaterial, .bskel or .banim (unrecognized magic)");
	}

	// A clip set's signature only means something next to the rig it names, so describe resolves it --
	// against the data root when one is given, and beside the file otherwise, which is how a standalone
	// baked directory is laid out.
	std::optional<assetlib::Skeleton>
	resolveSkeleton(
		const std::filesystem::path& animationFile,
		const std::string&           skeleton,
		const std::filesystem::path& dataRoot)
	{
		if (skeleton.empty())
			return std::nullopt;

		const std::filesystem::path base =
			dataRoot.empty() ? animationFile.parent_path() : dataRoot;

		std::error_code ec;
		if (!std::filesystem::exists(base / skeleton, ec))
			return std::nullopt;

		return assetlib::loadSkeleton(base / skeleton);
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
	std::string name       = "mesh";
	float       sampleRate = assetlib::c_DefaultSampleRate;

	auto* bake = app.add_subcommand(
		"bake",
		"Convert a glTF (.glb/.gltf) into a modular .bmesh + .ktx2 texture set, plus a .bskel and "
		".banim when it carries a skin");
	bake->add_option("input", input, "Source .glb/.gltf file")
		->required()
		->check(CLI::ExistingFile);
	bake->add_option("-o,--out", outDir, "Output directory")->required();
	bake->add_option("-n,--name", name, "Base name for the .bmesh (default: mesh)");
	bake->add_option(
			"-r,--sample-rate",
			sampleRate,
			"Hz every animation clip is resampled to (default: 30)")
		->check(CLI::PositiveNumber);

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

	auto* describe = app.add_subcommand(
		"describe",
		"Print the contents of a .bmesh, .bmaterial, .bskel or .banim as text");
	describe->add_option("input", describeInput, "Source container file")
		->required()
		->check(CLI::ExistingFile);
	describe->add_option(
		"-d,--data-root",
		describeDataRoot,
		"Project data directory the asset's paths resolve against. For a material this also stats "
		"each routed source, so a stale bake is reported; for a clip set it is where its skeleton "
		"is looked up");
	describe->add_flag(
		"-b,--brief",
		describeBrief,
		"Mesh only: print the summary and material table, but not every submesh");

	std::string refsDataRoot;
	std::string refsAsset;

	auto* refs = app.add_subcommand(
		"refs",
		"Print what references an asset, and whether it can therefore be deleted");
	refs->add_option("-d,--data-root", refsDataRoot, "Project data directory to scan")
		->required()
		->check(CLI::ExistingDirectory);
	refs->add_option(
		"asset",
		refsAsset,
		"Asset to report on, relative to the data root. Omitted, the whole project is summarised, "
		"and every dangling reference listed");

	std::string pruneDataRoot;
	std::string pruneTextureDir = "Textures";
	bool        pruneDryRun     = false;
	bool        pruneYes        = false;

	auto* prune = app.add_subcommand(
		"prune",
		"Delete the baked textures under a project's data root that no material references any "
		"more");
	prune->add_option("-d,--data-root", pruneDataRoot, "Project data directory to prune")
		->required()
		->check(CLI::ExistingDirectory);
	prune->add_option(
		"-t,--texture-dir",
		pruneTextureDir,
		"Directory the material bake writes into, relative to the data root (default: Textures)");
	prune->add_flag("--dry-run", pruneDryRun, "List what would be deleted and delete nothing");
	prune->add_flag("-y,--yes", pruneYes, "Delete without asking for confirmation");

	CLI11_PARSE(app, argc, argv);

	if (*bake)
	{
		try
		{
			const auto imported = assetlib::loadFromGltf(input, {}, sampleRate);
			assetlib::bake(imported, outDir, name);
			spdlog::info(
				"Baked '{}' -> {}/{}.bmesh ({} materials, {} textures)",
				input,
				outDir,
				name,
				imported.materials.size(),
				imported.textures.size());

			if (!imported.skeleton.bones.empty())
				spdlog::info(
					"Baked the rig -> {}/{} ({} bones, {} clips at {} Hz)",
					outDir,
					assetlib::skeletonFileName(name),
					imported.skeleton.bones.size(),
					imported.animations.clips.size(),
					sampleRate);
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
			switch (sniff(path))
			{
			case ContainerType::kMesh:
				std::cout << assetlib::describe(assetlib::load(path), !describeBrief);
				break;

			case ContainerType::kMaterial:
				std::cout << assetlib::describe(
					assetlib::loadMaterial(path),
					std::filesystem::path(describeDataRoot));
				break;

			case ContainerType::kSkeleton:
				std::cout << assetlib::describe(assetlib::loadSkeleton(path));
				break;

			case ContainerType::kAnimation:
			{
				const auto animations = assetlib::loadAnimations(path);
				const auto skeleton = resolveSkeleton(path, animations.skeleton, describeDataRoot);
				std::cout << assetlib::describe(animations, skeleton ? &*skeleton : nullptr);
				break;
			}
			}
		}
		catch (const std::exception& e)
		{
			spdlog::error("describe failed: {}", e.what());
			return 1;
		}
	}

	if (*refs)
	{
		try
		{
			auto desc     = assetlib::AssetRefScanDesc();
			desc.dataRoot = refsDataRoot;

			const auto graph = assetlib::AssetRefGraph::Scan(desc);

			spdlog::info(
				"Scanned {} meshes, {} materials and {} clip sets: {} references",
				graph.meshesScanned,
				graph.materialsScanned,
				graph.clipSetsScanned,
				graph.Edges().size());

			// The listing is the command's output, so it goes to stdout rather than through the logger.
			if (refsAsset.empty())
			{
				if (graph.broken.empty())
				{
					spdlog::info("No dangling references.");
					return 0;
				}

				std::cout << "Dangling (" << graph.broken.size() << "):\n";
				for (const assetlib::AssetRef& ref : graph.broken)
					std::cout << "  " << ref.referrer << " -> " << ref.target << " (missing)\n";
				std::cout << std::flush;
				return 0;
			}

			const auto plan = assetlib::planDeletion(graph, refsAsset);

			if (plan.Allowed())
			{
				std::cout << refsAsset << ": nothing references it; it can be deleted.\n"
						  << std::flush;
				return 0;
			}

			// Assets, not edges: one material routing four channels from four textures in a folder
			// holds it four times over, and reporting that as four referrers would read as a bug.
			auto holders = std::set<std::string>();
			for (const assetlib::AssetRef& ref : plan.blockers) holders.insert(ref.referrer);

			std::cout << refsAsset << ": referenced by " << holders.size()
					  << (holders.size() == 1 ? " asset" : " assets")
					  << ", so it cannot be deleted.\n";

			// Named with the target, because for a directory the referrer alone does not say what in it
			// is being held, and that is what the user has to go and re-route.
			for (const assetlib::AssetRef& ref : plan.blockers)
				std::cout << "  " << ref.referrer << ' ' << describeRefKind(ref.kind) << ' '
						  << ref.target << '\n';

			std::cout << std::flush;
		}
		catch (const std::exception& e)
		{
			spdlog::error("refs failed: {}", e.what());
			return 1;
		}
	}

	if (*prune)
	{
		try
		{
			auto desc       = assetlib::TexturePruneDesc();
			desc.dataRoot   = pruneDataRoot;
			desc.textureDir = pruneTextureDir;

			const auto scan = assetlib::findUnusedBakedTextures(desc);

			spdlog::info(
				"Scanned {} materials: {} baked maps still referenced, {} present in '{}'",
				scan.materialsScanned,
				scan.liveMaps,
				scan.candidates,
				pruneTextureDir);

			if (scan.unused.empty())
			{
				spdlog::info("Nothing to prune.");
				return 0;
			}

			// The listing is the command's output, so it goes to stdout rather than through the logger.
			std::cout << "Unused (" << scan.unused.size() << ", " << formatBytes(scan.bytes)
					  << "):\n";
			for (const assetlib::UnusedTexture& texture : scan.unused)
				std::cout << "  " << texture.path << "  (" << formatBytes(texture.bytes) << ")\n";
			std::cout << std::flush;

			if (pruneDryRun)
			{
				spdlog::info("Dry run: nothing deleted. Re-run without --dry-run to delete.");
				return 0;
			}

			if (!pruneYes && !confirm(
								 "Delete " + std::to_string(scan.unused.size()) +
								 " unused baked textures (" + formatBytes(scan.bytes) + ")?"))
			{
				spdlog::info("Cancelled: nothing deleted.");
				return 0;
			}

			const auto result = assetlib::deleteUnusedBakedTextures(scan, desc);

			spdlog::info(
				"Deleted {} textures, reclaiming {}",
				result.deleted,
				formatBytes(result.bytes));

			if (!result.failed.empty())
			{
				for (const std::string& path : result.failed)
					spdlog::error("could not delete '{}'", path);
				return 1;
			}
		}
		catch (const std::exception& e)
		{
			spdlog::error("prune failed: {}", e.what());
			return 1;
		}
	}

	return 0;
}
