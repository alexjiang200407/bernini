#include <CLI/CLI.hpp>
#include <assetlib/asset_describe.h>
#include <assetlib/asset_refs.h>
#include <assetlib/assetlib.h>
#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/env_bake.h>
#include <assetlib/env_import.h>
#include <assetlib/envmap_bake.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/skeleton.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/magic.h>
#include <spdlog/spdlog.h>

namespace
{
	enum class ContainerType
	{
		kMesh,
		kMaterial,
		kEnv,
		kSky,
		kEnvLighting,
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
		case assetlib::RefKind::kEnvironmentPart:
			return "composes";
		case assetlib::RefKind::kEnvSource:
			return "bakes its radiance from";
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
		constexpr uint32_t c_SkeletonMagic  = 0x4C4B5342u;  // 'B','S','K','L'
		constexpr uint32_t c_AnimationMagic = 0x4D4E4142u;  // 'B','A','N','M'

		std::ifstream in(path, std::ios::binary);
		uint32_t      magic = 0;
		if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)))
			throw std::runtime_error("cannot read the file header of " + path.string());

		switch (magic)
		{
		case assetlib::magic::c_BMesh:
			return ContainerType::kMesh;
		case assetlib::magic::c_BMaterial:
			return ContainerType::kMaterial;
		case assetlib::magic::c_BEnv:
			return ContainerType::kEnv;
		case assetlib::magic::c_BSky:
			return ContainerType::kSky;
		case assetlib::magic::c_BEnvL:
			return ContainerType::kEnvLighting;
		case c_SkeletonMagic:
			return ContainerType::kSkeleton;
		case c_AnimationMagic:
			return ContainerType::kAnimation;
		}

		throw std::runtime_error(
			path.string() +
			" is not a container this tool knows (expected .bmesh, .bmaterial, .benv, .bsky, "
			".benvl, .bskel or .banim)");
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

	std::string envInput;
	std::string envOut;
	std::string envCube;
	std::string envIem;
	uint32_t    envIemSize    = 128;
	uint32_t    envSkyboxSize = 512;
	float       envSkyboxBlur = 0.0f;
	uint32_t    envSize       = 256;
	uint32_t    envMips       = 7;
	uint32_t    envSamples    = 128;
	uint32_t    envThreads    = 0;

	auto* envmap = app.add_subcommand(
		"envmap",
		"Prefilter a radiance cube map into the GGX split-sum chain the shader samples");
	envmap->add_option("input", envInput, "Source .hdr (equirectangular) or cube map .ktx2")
		->required()
		->check(CLI::ExistingFile);
	envmap->add_option("-o,--out", envOut, "Write the prefilter chain here as a loose .ktx2");
	envmap->add_option(
		"-c,--cube",
		envCube,
		"Also write the unfiltered source cube here -- this is the skybox");
	envmap->add_option("-i,--irradiance", envIem, "Also write the irradiance map here");
	envmap->add_option("--irradiance-size", envIemSize, "Irradiance face size (default: 128)");
	envmap->add_option(
		"-s,--size",
		envSize,
		"Prefilter base face size (default: 256). The lobe blurs it, so this can be modest");
	envmap->add_option(
		"--skybox-size",
		envSkyboxSize,
		"Skybox face size (default: 512). Seen directly at viewport resolution, so it wants more "
		"than the prefilter -- but no more than the source can supply, and less if it is blurred");
	envmap->add_option(
		"--skybox-blur",
		envSkyboxBlur,
		"GGX roughness to defocus the skybox by (0 = sharp). Reads as depth of field, and hides a "
		"source that cannot fill the face");
	envmap->add_option("-m,--mips", envMips, "Mip count; must match MAX_REFLECTION_LOD + 1");
	envmap->add_option("-n,--samples", envSamples, "GGX samples per texel (default: 128)");
	envmap->add_option(
		"-j,--threads",
		envThreads,
		"Worker threads (default: hardware concurrency)");

	std::string envProject;
	std::string envName = "env";
	envmap->add_option(
		"-p,--project",
		envProject,
		"Write the split formats into this project data root instead: float sources into "
		"textures_src/, a baked Sky/<name>.bsky and EnvLighting/<name>.benvl, and an "
		"Environments/<name>.benv composing the pair");
	envmap->add_option("--name", envName, "Asset name for --project outputs (default: env)");

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
		app.add_subcommand("describe", "Print the contents of an asset container as text");
	describe->add_option("input", describeInput, "Source container file")
		->required()
		->check(CLI::ExistingFile);
	describe->add_option(
		"-d,--data-root",
		describeDataRoot,
		"Project data directory the asset's paths resolve against. For a material, a sky or a "
		"lighting this also stats each routed source, so a stale bake is reported; for an "
		"environment it reports whether the files it names are there, and for a clip set it is "
		"where its skeleton is looked up");
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

	std::string stripInput;
	std::string stripOut;
	bool        stripYes = false;

	auto* strip = app.add_subcommand(
		"strip",
		"Drop a material's authoring data, leaving the shippable form: the baked triplet, the "
		"factors and the name");
	strip->add_option("input", stripInput, "Source .bmaterial file")
		->required()
		->check(CLI::ExistingFile);
	strip->add_option(
		"-o,--out",
		stripOut,
		"Write here instead of rewriting the input. The routes and the node graph are not "
		"recoverable, so prefer this over stripping a material you still intend to author");
	strip->add_flag("-y,--yes", stripYes, "Rewrite the input without asking for confirmation");

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

	if (*envmap)
	{
		try
		{
			if (envOut.empty() && envProject.empty())
				throw std::runtime_error("nothing to write: pass --out and/or --project");

			const bool fromHdr = std::filesystem::path(envInput).extension() == ".hdr";

			// Projected at the skybox's size, which is the largest of the three: the prefilter and
			// the irradiance convolve it down anyway, and starting them from the finer cube costs
			// only the projection.
			assetlib::ImageData src = fromHdr ? assetlib::equirectToCube(
													assetlib::loadRadianceHdr(envInput),
													std::max(envSkyboxSize, envSize)) :
			                                    assetlib::loadKTX2(envInput);

			// Blurred, the skybox is a separate convolution of the same environment; sharp, it is
			// the projection itself. Either way the prefilter and irradiance still read `src`, so a
			// defocused background never reaches the lighting.
			assetlib::ImageData sky =
				envSkyboxBlur > 0.0f ?
					assetlib::blurCube(src, envSkyboxSize, envSkyboxBlur, 256, envThreads) :
					assetlib::ImageData();

			if (!envCube.empty())
			{
				const assetlib::ImageData& cube = envSkyboxBlur > 0.0f ? sky : src;
				assetlib::writeKTX2(cube, envCube, false, assetlib::Ktx2Compression::kNone);
				spdlog::info("Wrote the skybox cube to '{}' ({}^2)", envCube, cube.width);
			}

			assetlib::ImageData iem = assetlib::irradianceSh(src, envIemSize);
			if (!envIem.empty())
			{
				assetlib::writeKTX2(iem, envIem, false, assetlib::Ktx2Compression::kNone);
				spdlog::info("Wrote the irradiance map to '{}' ({}^2)", envIem, envIemSize);
			}

			auto desc      = assetlib::PrefilterDesc();
			desc.faceSize  = envSize;
			desc.mipLevels = envMips;
			desc.samples   = envSamples;
			desc.threads   = envThreads;

			auto                stats = assetlib::PrefilterStats();
			assetlib::ImageData out   = assetlib::prefilterRadiance(src, desc, &stats);

			if (!envOut.empty())
				assetlib::writeKTX2(out, envOut, false, assetlib::Ktx2Compression::kNone);

			spdlog::info(
				"Prefiltered '{}' ({}^2) -> '{}' ({}^2 x {} mips) in {:.2f}s",
				envInput,
				src.width,
				envOut,
				desc.faceSize,
				desc.mipLevels,
				stats.seconds);
			spdlog::info(
				"  {} texels, {} GGX samples, {:.1f}M samples/s",
				stats.texelsWritten,
				stats.samplesTaken,
				stats.seconds > 0.0 ?
					static_cast<double>(stats.samplesTaken) / stats.seconds / 1e6 :
					0.0);

			if (!envProject.empty())
			{
				auto importDesc               = assetlib::EnvImportDesc();
				importDesc.dataRoot           = envProject;
				importDesc.source             = envInput;
				importDesc.name               = envName;
				importDesc.skyFaceSize        = envSkyboxSize;
				importDesc.skyBlur            = envSkyboxBlur;
				importDesc.prefilterFaceSize  = envSize;
				importDesc.prefilterMips      = envMips;
				importDesc.prefilterSamples   = envSamples;
				importDesc.irradianceFaceSize = envIemSize;
				importDesc.threads            = envThreads;

				const assetlib::EnvImportResult imported = assetlib::importEnvironment(importDesc);

				spdlog::info(
					"Imported '{}' into '{}': {} files, exposure {:.3f}",
					envName,
					envProject,
					imported.written.size(),
					imported.exposure);

				for (const std::string& file : imported.written) spdlog::info("  wrote {}", file);
			}
		}
		catch (const std::exception& e)
		{
			spdlog::error("envmap failed: {}", e.what());
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
			const auto dataRoot = std::filesystem::path(describeDataRoot);

			switch (sniff(path))
			{
			case ContainerType::kMesh:
				std::cout << assetlib::describe(assetlib::load(path), !describeBrief);
				break;
			case ContainerType::kMaterial:
				std::cout << assetlib::describe(assetlib::loadMaterial(path), dataRoot);
				break;
			case ContainerType::kEnv:
				std::cout << assetlib::describe(assetlib::loadEnv(path), dataRoot);
				break;
			case ContainerType::kSky:
				std::cout << assetlib::describe(assetlib::loadSky(path), dataRoot);
				break;
			case ContainerType::kEnvLighting:
				std::cout << assetlib::describe(assetlib::loadEnvLighting(path), dataRoot);
				break;
			case ContainerType::kSkeleton:
				std::cout << assetlib::describe(assetlib::loadSkeleton(path));
				break;
			case ContainerType::kAnimation:
			{
				const auto animations = assetlib::loadAnimations(path);
				const auto skeleton   = resolveSkeleton(path, animations.skeleton, dataRoot);
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

	if (*strip)
	{
		try
		{
			const std::filesystem::path in = stripInput;
			const std::filesystem::path out =
				stripOut.empty() ? in : std::filesystem::path(stripOut);

			assetlib::BMaterial material = assetlib::loadMaterial(in);

			// Asked before the strip, not after: the routes and the graph are the only record of how
			// the material was authored, and rewriting the input destroys them.
			if (out == in && !stripYes &&
			    !confirm(
					"Rewrite '" + in.string() +
					"' without its routes or node graph? This cannot be undone."))
			{
				spdlog::info("Left '{}' alone.", in.string());
				return 0;
			}

			// Throws when the material has never been baked, leaving it untouched -- so the file is
			// only written once there is a shippable form to write.
			assetlib::stripAuthoringData(material);
			assetlib::saveMaterial(material, out);

			spdlog::info("Stripped '{}' -> '{}'", in.string(), out.string());
		}
		catch (const std::exception& e)
		{
			spdlog::error("strip failed: {}", e.what());
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
				"Scanned {} meshes, {} materials, {} environment assets and {} clip sets: {} "
				"references",
				graph.meshesScanned,
				graph.materialsScanned,
				graph.environmentsScanned,
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
				"Scanned {} materials and {} environment assets: {} baked maps still referenced, "
				"{} present in '{}'",
				scan.materialsScanned,
				scan.environmentsScanned,
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
