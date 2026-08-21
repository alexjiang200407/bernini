#include <CLI/CLI.hpp>
#include <assetlib/AssetStore.h>
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
#include <assetlib/bvat_io.h>
#include <assetlib/container_format.h>
#include <assetlib/container_info.h>
#include <assetlib/env_bake.h>
#include <assetlib/env_import.h>
#include <assetlib/envmap_bake.h>
#include <assetlib/image_io.h>
#include <assetlib/material_bake.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/migrate.h>
#include <assetlib/pak_io.h>
#include <assetlib/pak_pack.h>
#include <assetlib/rebake_bounds.h>
#include <assetlib/skeleton.h>
#include <assetlib/texture_prune.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <core/file/LooseFileSystem.h>
#include <core/file/file.h>
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
		kVat,
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
		if (unit == 0)
			std::snprintf(text, sizeof(text), "%.0f %s", value, c_Units[unit]);
		else
			std::snprintf(text, sizeof(text), "%.1f %s", value, c_Units[unit]);
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
		case assetlib::RefKind::kVatSource:
			return "was baked from";
		}

		return "references";
	}

	// Every container opens with a 4-byte magic, so the type is read from the file rather than guessed
	// from its extension -- `describe` then works on a file named anything. This is the deliberate
	// opposite of assetlib::assetTypeFromExtension, which never opens the file.
	ContainerType
	sniff(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		uint32_t      magic = 0;
		if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic)))
			core::throw_runtime_error("cannot read the file header of {}", path.string());

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
		case assetlib::magic::c_BSkel:
			return ContainerType::kSkeleton;
		case assetlib::magic::c_BAnim:
			return ContainerType::kAnimation;
		case assetlib::magic::c_BVat:
			return ContainerType::kVat;
		}

		core::throw_runtime_error(
			"{} is not a container this tool knows (expected .bmesh, .bmaterial, .benv, .bsky, "
			".benvl, .bskel, .banim or .bvat)",
			path.string());
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
	app.set_version_flag("--version", assetlib::version());
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

	std::string vatDataRoot;
	std::string vatMesh;
	std::string vatAnimations;
	std::string vatOut;

	auto* bakevat = app.add_subcommand(
		"bakevat",
		"Bake a rig's clips into a .bvat: every skinned vertex at every frame, as a position and a "
		"normal texture the crowd tier fetches instead of skinning");
	bakevat
		->add_option(
			"-d,--data-root",
			vatDataRoot,
			"Directory the mesh's asset paths resolve "
			"against: the project's Data directory, or the baked directory itself for assetlib_cli "
			"bake output")
		->required()
		->check(CLI::ExistingDirectory);
	bakevat->add_option("mesh", vatMesh, "A .bmesh, relative to the data root")->required();
	bakevat
		->add_option("animations", vatAnimations, "The .banim to bake, relative to the data root")
		->required();
	bakevat->add_option(
		"-o,--out",
		vatOut,
		"Output .bvat (default: beside the mesh, named for the (mesh, clip set) pair -- where the "
		"runtime loads from)");

	std::string envInput;
	std::string envOut;
	std::string envCube;
	std::string envIem;
	uint32_t    envIemSize    = 128;
	uint32_t    envSkyboxSize = 512;
	uint32_t    envSkyboxMips = 6;
	uint32_t    envSkyboxMip  = 0;
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
		"--skybox-mips",
		envSkyboxMips,
		"Levels in the skybox's defocus chain (default: 6). Mip 0 is the sharp projection; each "
		"level below it is convolved to the width its own texel subtends");
	envmap->add_option(
		"--skybox-mip",
		envSkyboxMip,
		"Which level the written .bsky presents (default: 0 = sharp). Reads as depth of field, and "
		"hides a source that cannot fill the face -- and unlike a baked blur it is reversible");
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

	std::string tangentsInput;

	auto* tangents = app.add_subcommand(
		"tangents",
		"Derive a tangent basis for every submesh of a .bmesh that has none, in place");
	tangents->add_option("input", tangentsInput, "Source .bmesh file")
		->required()
		->check(CLI::ExistingFile);

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
	bool describeSchema = false;
	describe->add_flag(
		"-s,--schema",
		describeSchema,
		"Also print the file's format number and the schema it was written with -- every struct it "
		"stores, field by field -- which is what an older file actually holds");

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

	std::string packDataRoot;
	std::string packTarget;

	auto* pack = app.add_subcommand(
		"pack",
		"Write a project's data root into one .bpak archive of everything the runtime reads");
	pack->add_option("-d,--data-root", packDataRoot, "Project data directory to pack")
		->required()
		->check(CLI::ExistingDirectory);
	pack->add_option(
		"-o,--out",
		packTarget,
		"Archive to write (default: Data.bpak beside the data root)");

	std::string listArchive;

	auto* list = app.add_subcommand("list", "Print the entry table of a .bpak");
	list->add_option("archive", listArchive, "The .bpak to read")
		->required()
		->check(CLI::ExistingFile);

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

	std::string migrateRoot;
	bool        migrateDryRun = false;
	bool        migrateYes    = false;

	auto* migrate = app.add_subcommand(
		"migrate",
		"Re-save every container under a project's data root at the current schema. A file that is "
		"already current is left untouched, so running it twice rewrites nothing the second time; "
		"a "
		"file that cannot be read is reported and skipped");
	migrate->add_option("data-root", migrateRoot, "Project data directory")
		->required()
		->check(CLI::ExistingDirectory);
	migrate->add_flag(
		"-n,--dry-run",
		migrateDryRun,
		"Report what would be rewritten; write nothing");
	migrate->add_flag("-y,--yes", migrateYes, "Rewrite without asking for confirmation");

	std::string boundsRoot;
	bool        boundsDryRun = false;
	bool        boundsYes    = false;

	auto* bakebounds = app.add_subcommand(
		"bakebounds",
		"Bake every rig's posed culling boxes into its .banim, measured against every .bmesh that "
		"names its skeleton -- the retrofit for a project imported before loads could read them. "
		"A clip set whose boxes are current is left untouched");
	bakebounds->add_option("data-root", boundsRoot, "Project data directory")
		->required()
		->check(CLI::ExistingDirectory);
	bakebounds->add_flag(
		"-n,--dry-run",
		boundsDryRun,
		"Report what would be rewritten; write nothing");
	bakebounds->add_flag("-y,--yes", boundsYes, "Rewrite without asking for confirmation");

	std::string expInput;
	float       expSet   = 0.0f;
	bool        expClear = false;

	auto* exposure = app.add_subcommand(
		"exposure",
		"Show or author the exposure a .benvl renders at, overruling the value its bake derived");
	exposure->add_option("input", expInput, "A .benvl file")->required()->check(CLI::ExistingFile);
	auto* expSetOpt = exposure->add_option(
		"-s,--set",
		expSet,
		"Author this exposure. Survives a re-bake, which refreshes only the derivation");
	exposure->add_flag("-c,--clear", expClear, "Drop the authored value and fall back to the bake")
		->excludes(expSetOpt);

	CLI11_PARSE(app, argc, argv);

	if (*bake)
	{
		try
		{
			const auto imported = assetlib::loadFromGltf(input, {}, sampleRate);
			const auto derived  = assetlib::bake(imported, outDir, name);

			if (derived.skipped > 0)
				spdlog::warn(
					"{} submesh(es) have no tangent and no way to derive one (no normals, no UVs, "
					"or no triangles) -- a normal map on those will not render",
					derived.skipped);

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

	if (*bakevat)
	{
		try
		{
			auto desc       = assetlib::VatBakeDesc();
			desc.mesh       = vatMesh;
			desc.animations = vatAnimations;

			const assetlib::BVat vat = assetlib::bakeVat(assetlib::AssetStore(vatDataRoot), desc);

			const std::filesystem::path out = vatOut.empty() ?
			                                      std::filesystem::path(vatDataRoot) /
			                                          assetlib::vatPathFor(vatMesh, vatAnimations) :
			                                      std::filesystem::path(vatOut);
			assetlib::saveVat(vat, out);

			spdlog::info(
				"Baked '{}' + '{}' -> '{}': {} x {} texels, {} clip(s), {} bones",
				vatMesh,
				vatAnimations,
				out.string(),
				vat.width,
				vat.height,
				vat.clips.size(),
				vat.boneCount);
			spdlog::info(
				"  positions {}, normals {}, palettes {}",
				formatBytes(vat.positionsKtx2.size()),
				formatBytes(vat.normalsKtx2.size()),
				formatBytes(vat.palettes.size() * sizeof(glm::mat4)));
		}
		catch (const std::exception& e)
		{
			spdlog::error("bakevat failed: {}", e.what());
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

			// A shipped map is RGB9E5, and that is the only form left when a route's float source
			// has gone. Re-convolving one costs a generation of quantization, so it is a recovery
			// path and not the one to reach for when the source is still there.
			if (src.vkFormat == assetlib::VkFormat::E5B9G9R9_UFLOAT_PACK32)
			{
				spdlog::warn(
					"'{}' is RGB9E5; unpacking it to float. Re-convolving a baked map quantizes "
					"twice -- prefer the source it was baked from",
					envInput);
				src = assetlib::unpackRgb9e5(src);
			}

			if (!envCube.empty())
			{
				// A defocus chain rather than one blurred mip: which level the backdrop draws is a
				// viewer's choice, so it must survive the bake. The prefilter and irradiance still
				// read `src`, so nothing the backdrop does reaches the lighting.
				const assetlib::ImageData cube =
					assetlib::skyChain(src, envSkyboxSize, envSkyboxMips, 256, envThreads);
				assetlib::writeKTX2(cube, envCube, false, assetlib::Ktx2Compression::kNone);
				spdlog::info(
					"Wrote the skybox cube to '{}' ({}^2 x {} mips)",
					envCube,
					cube.width,
					envSkyboxMips);
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
				importDesc.skyMips            = envSkyboxMips;
				importDesc.skyMipLevel        = envSkyboxMip;
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

	if (*tangents)
	{
		try
		{
			assetlib::BMesh mesh   = assetlib::load(tangentsInput);
			const auto      result = assetlib::generateTangents(mesh);

			if (result.generated > 0)
				assetlib::save(mesh, tangentsInput);

			spdlog::info(
				"'{}': {} submesh(es) gained a tangent, {} already had one, {} could not have one "
				"derived (no normals, no UVs, or no triangles)",
				tangentsInput,
				result.generated,
				result.kept,
				result.skipped);
		}
		catch (const std::exception& e)
		{
			spdlog::error("tangent generation failed: {}", e.what());
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

			// Optional: a container describes on its own, and only a root lets the stamps be checked
			// against what is actually there.
			std::optional<assetlib::AssetStore> store;
			if (!dataRoot.empty())
				store.emplace(dataRoot);

			// With a project the stamps are checked against what is on disk; without one only what
			// the container records can be reported.
			const auto describeAsset = [&store](const auto& asset) {
				return store.has_value() ? store->Describe(asset) : assetlib::describe(asset);
			};

			if (describeSchema)
			{
				const auto info =
					assetlib::inspectContainer(core::file::read_file_bytes(path.string()));
				std::cout
					<< std::format("format {}.{}\nschema\n", info.versionMajor, info.versionMinor)
					<< assetlib::describe(info.schema) << '\n';
			}

			switch (sniff(path))
			{
			case ContainerType::kMesh:
				std::cout << assetlib::describe(assetlib::load(path), !describeBrief);
				break;
			case ContainerType::kMaterial:
				std::cout << describeAsset(assetlib::loadMaterial(path));
				break;
			case ContainerType::kEnv:
				std::cout << describeAsset(assetlib::loadEnv(path));
				break;
			case ContainerType::kSky:
				std::cout << describeAsset(assetlib::loadSky(path));
				break;
			case ContainerType::kEnvLighting:
				std::cout << describeAsset(assetlib::loadEnvLighting(path));
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
			case ContainerType::kVat:
				// Tables only: the pixel chunks are tens of MB and describe never reads a texel.
				std::cout << describeAsset(assetlib::loadVatTables(path));
				break;
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

	if (*migrate)
	{
		try
		{
			const std::filesystem::path root(migrateRoot);

			const auto print = [&](const assetlib::MigrateReport& report, bool preview) {
				for (const auto& file : report.files)
				{
					const auto relative =
						std::filesystem::relative(file.path, root).generic_string();
					switch (file.outcome)
					{
					case assetlib::MigratedFile::Outcome::kUnchanged:
						break;
					case assetlib::MigratedFile::Outcome::kRewritten:
						std::cout << (preview ? "would rewrite  " : "rewrote        ") << relative
								  << '\n';
						break;
					case assetlib::MigratedFile::Outcome::kFailed:
						std::cout << "cannot convert " << relative << ": " << file.message << '\n';
						break;
					}
				}
				std::cout << std::format(
					"{} unchanged, {} {}, {} cannot be converted\n",
					report.Count(assetlib::MigratedFile::Outcome::kUnchanged),
					report.Count(assetlib::MigratedFile::Outcome::kRewritten),
					preview ? "to rewrite" : "rewritten",
					report.Count(assetlib::MigratedFile::Outcome::kFailed));
			};

			// The preview walk never writes, so a read-only file or a full disk shows up only in the
			// real one -- which is why the real walk's report is the one printed last.
			const auto preview = assetlib::migrateProject(root, true);
			print(preview, true);
			if (migrateDryRun)
				return preview.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;

			const auto toRewrite = preview.Count(assetlib::MigratedFile::Outcome::kRewritten);
			if (toRewrite == 0)
				return preview.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;
			if (!migrateYes && !confirm(std::format("Rewrite {} file(s) in place?", toRewrite)))
			{
				spdlog::info("Left '{}' alone.", root.string());
				return 0;
			}

			const auto report = assetlib::migrateProject(root, false);
			print(report, false);
			return report.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("migrate failed: {}", e.what());
			return 1;
		}
	}

	if (*bakebounds)
	{
		try
		{
			const std::filesystem::path root(boundsRoot);

			const auto print = [&](const assetlib::RebakeBoundsReport& report, bool preview) {
				for (const auto& file : report.files)
				{
					const auto relative =
						std::filesystem::relative(file.path, root).generic_string();
					switch (file.outcome)
					{
					case assetlib::RebakedFile::Outcome::kCurrent:
						break;
					case assetlib::RebakedFile::Outcome::kRebaked:
						std::cout << (preview ? "would rebake " : "rebaked      ") << relative
								  << '\n';
						break;
					case assetlib::RebakedFile::Outcome::kOrphaned:
						std::cout << "no mesh skins " << relative << '\n';
						break;
					case assetlib::RebakedFile::Outcome::kFailed:
						std::cout << "cannot rebake " << relative << ": " << file.message << '\n';
						break;
					}
				}
				std::cout << std::format(
					"{} current, {} {}, {} orphaned, {} cannot be rebaked\n",
					report.Count(assetlib::RebakedFile::Outcome::kCurrent),
					report.Count(assetlib::RebakedFile::Outcome::kRebaked),
					preview ? "to rebake" : "rebaked",
					report.Count(assetlib::RebakedFile::Outcome::kOrphaned),
					report.Count(assetlib::RebakedFile::Outcome::kFailed));
			};

			// The preview costs a signature per mesh; only the real walk pays the measure, and a
			// write that fails shows up only there -- so its report is the one printed last.
			const auto preview = assetlib::rebakePosedBounds(root, true);
			print(preview, true);
			if (boundsDryRun)
				return preview.Count(assetlib::RebakedFile::Outcome::kFailed) == 0 ? 0 : 1;

			const auto toRebake = preview.Count(assetlib::RebakedFile::Outcome::kRebaked);
			if (toRebake == 0)
				return preview.Count(assetlib::RebakedFile::Outcome::kFailed) == 0 ? 0 : 1;
			if (!boundsYes &&
			    !confirm(
					std::format(
						"Rebake {} clip set(s) in place? Each is seconds of CPU skinning.",
						toRebake)))
			{
				spdlog::info("Left '{}' alone.", root.string());
				return 0;
			}

			const auto report = assetlib::rebakePosedBounds(root, false);
			print(report, false);
			return report.Count(assetlib::RebakedFile::Outcome::kFailed) == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("bakebounds failed: {}", e.what());
			return 1;
		}
	}

	if (*refs)
	{
		try
		{
			const assetlib::AssetStore store{ std::filesystem::path(refsDataRoot) };

			const auto graph = assetlib::AssetRefGraph::Scan(store);

			spdlog::info(
				"Scanned {} meshes, {} materials, {} environment assets, {} clip sets and {} VAT "
				"bakes: {} references",
				graph.meshesScanned,
				graph.materialsScanned,
				graph.environmentsScanned,
				graph.clipSetsScanned,
				graph.vatsScanned,
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

	if (*pack)
	{
		try
		{
			const assetlib::AssetStore store{ std::filesystem::path(packDataRoot) };

			auto desc   = assetlib::PackDesc();
			desc.target = packTarget.empty() ? std::filesystem::path(packDataRoot).parent_path() /
			                                       assetlib::c_DefaultArchiveName :
			                                   std::filesystem::path(packTarget);

			const assetlib::PackReport report = assetlib::packProject(store, desc);

			if (report.vatsRebaked != 0)
				spdlog::info("Re-baked {} stale .bvat before packing", report.vatsRebaked);

			spdlog::info(
				"Packed {} entries, {} MB of payload, into '{}'",
				report.entries,
				report.payloadBytes / (1024ull * 1024),
				desc.target.string());

			if (!report.materialsDrawingLoose.empty())
			{
				spdlog::warn(
					"{} of the packed materials still draw from authoring sources, which an "
					"archive does not carry -- bake them or they ship untextured:",
					report.materialsDrawingLoose.size());

				for (const std::string& material : report.materialsDrawingLoose)
					spdlog::warn("  {}", material);
			}

			// Said rather than left implicit: an extension nothing claims is an extension the
			// archive does not carry, and a runtime container missing from every archive is a
			// failure that would otherwise surface as a missing asset at load.
			std::vector<std::pair<std::string, uint32_t>> skipped(
				report.skippedByExtension.begin(),
				report.skippedByExtension.end());
			std::ranges::sort(skipped);

			for (const auto& [extension, count] : skipped)
			{
				spdlog::info(
					"  skipped {:>4} x '{}' -- no asset type claims it",
					count,
					extension.empty() ? "(no extension)" : extension);
			}
		}
		catch (const std::exception& e)
		{
			spdlog::error("pack failed: {}", e.what());
			return 1;
		}
	}

	if (*list)
	{
		try
		{
			const assetlib::PakFile archive{ std::filesystem::path(listArchive) };

			// Straight to stdout: this is the command's output, and it is meant to diff.
			for (const std::string& entry : archive.Enumerate())
			{
				const core::file::FileStamp stamp = archive.Stat(entry).value();
				std::cout << std::format("{:>12}  {:>12}  {}\n", stamp.size, stamp.mtime, entry);
			}

			std::cout << std::flush;
		}
		catch (const std::exception& e)
		{
			spdlog::error("list failed: {}", e.what());
			return 1;
		}
	}

	if (*prune)
	{
		try
		{
			const assetlib::AssetStore store{ std::filesystem::path(pruneDataRoot) };

			auto desc       = assetlib::TexturePruneDesc();
			desc.textureDir = pruneTextureDir;

			const auto scan = assetlib::findUnusedBakedTextures(store, desc);

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

			const auto result = assetlib::deleteUnusedBakedTextures(scan, store);

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

	if (*exposure)
	{
		try
		{
			assetlib::BEnvLighting lighting = assetlib::loadEnvLighting(expInput);

			if (*expSetOpt || expClear)
			{
				if (expClear)
					lighting.exposureOverride.reset();
				else
					lighting.exposureOverride = expSet;

				assetlib::saveEnvLighting(lighting, expInput);
			}

			spdlog::info(
				"'{}': derived {:.6g}, authored {}, rendering at {:.6g}",
				expInput,
				lighting.exposure,
				lighting.exposureOverride ? std::format("{:.6g}", *lighting.exposureOverride) :
											std::string("(none)"),
				lighting.EffectiveExposure());
		}
		catch (const std::exception& e)
		{
			spdlog::error("exposure failed: {}", e.what());
			return 1;
		}
	}

	return 0;
}
