#include <CLI/CLI.hpp>
#include <assetlib/AssetCodec.h>
#include <assetlib/AssetStore.h>
#include <assetlib/Project.h>
#include <assetlib/asset_import.h>
#include <assetlib/asset_refs.h>
#include <assetlib/assetlib.h>
#include <assetlib/avatar.h>
#include <assetlib/bmesh.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/envmap.h>
#include <assetlib/material_bake.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/migrate.h>
#include <assetlib/pak.h>
#include <assetlib/project_layout.h>
#include <assetlib/rebake_bounds.h>
#include <assetlib/skinning.h>
#include <assetlib/texture_prune.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/magic.h>
#include <core/err/util.h>
#include <core/file/file.h>
#include <core/profiling/MemoryReport.h>
#include <core/str/str.h>
#include <spdlog/spdlog.h>

namespace
{

	// Every container the table knows, for a message that cannot drift from what sniff accepts.
	std::string
	knownContainers()
	{
		std::string list;
		for (const assetlib::ContainerKind& kind : assetlib::containerKinds())
		{
			if (!list.empty())
				list += ", ";
			list += kind.extension;
		}
		return list;
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
		case assetlib::RefKind::kDocumentSkeleton:
			return "binds its source's joints to";
		case assetlib::RefKind::kDocumentOutput:
			return "produced";
		case assetlib::RefKind::kClipSkeleton:
			return "was resampled against";
		case assetlib::RefKind::kImportedSource:
			return "was imported from";
		case assetlib::RefKind::kAvatarSkeleton:
			return "authors the legs of";
		}

		return "references";
	}

	// Every container opens with a 4-byte magic, so the type is read from the file rather than guessed
	// from its extension -- `describe` then works on a file named anything. This is the deliberate
	// opposite of assetlib::assetTypeFromExtension, which never opens the file. The exception is an
	// authored text document, which has no magic to read: there the extension is the identity, as it
	// is for the loaders.
	assetlib::AssetType
	sniff(const assetlib::AssetStore& store, std::string_view key)
	{
		const auto stamp = store.GetFiles().Stat(key);
		core::throw_runtime_error_if(!stamp.has_value(), "{} does not exist", key);
		core::throw_runtime_error_if(
			stamp->size < sizeof(uint32_t),
			"{} is too short to be a container",
			key);

		// 16 bytes rather than the magic's four: a text document may open with whitespace, and
		// the dispatch must agree with what the loaders will read.
		const std::vector<std::byte> header =
			store.GetFiles().ReadRange(key, 0, std::min<uint64_t>(16, stamp->size));

		if (assetlib::isTextAssetDocument(header))
		{
			// A document opens with its content, so only its name can say which it is.
			const auto type = assetlib::assetTypeFromExtension(std::filesystem::path(key));
			if (type == assetlib::AssetType::kMaterial ||
			    type == assetlib::AssetType::kEnvironment || type == assetlib::AssetType::kAvatar)
				return *type;

			core::throw_runtime_error(
				"{} is a text document, and the only text containers this tool knows are "
				".bmaterial, .benv and .bavatar",
				key);
		}

		uint32_t magic = 0;
		std::memcpy(&magic, header.data(), sizeof(magic));

		const auto kind = assetlib::containerKindForMagic(magic);
		core::throw_runtime_error_if(
			!kind.has_value(),
			"{} is not a container this tool knows (expected one of: {})",
			key,
			knownContainers());

		return kind->type;
	}

	// A clip set's signature only means something next to the rig it names, so describe resolves it
	// through the project the clips live in.
	std::optional<assetlib::Skeleton>
	resolveSkeleton(const assetlib::AssetStore& store, const std::string& skeleton)
	{
		if (skeleton.empty() || !store.Exists(skeleton))
			return std::nullopt;

		return store.Load<assetlib::Skeleton>(skeleton);
	}
}

int
main(int argc, char** argv)
{
	CLI::App app{ "Bernini asset pipeline CLI" };
	app.set_version_flag("--version", assetlib::version());
	app.require_subcommand(1);

	// Armed on request rather than on every run, unlike the editor's: this tool has no log file, so
	// its report would land in the terminal of every scripted `describe`.
	std::string memReportPath;
	app.add_option(
		"--mem-report",
		memReportPath,
		"Write the memory report to this JSON file when the command finishes");

	// One project, named the same way by every command that addresses one. A command's asset
	// arguments are then mount keys inside it -- never host paths, which is what let a directory that
	// was not a project be passed and silently accepted.
	std::string projectFile;
	const auto  addProject = [&projectFile](CLI::App* command) {
		command->add_option("-p,--project", projectFile, "The .bproj to work in")
			->required()
			->check(CLI::ExistingFile);
	};

	std::string input;
	std::string name;
	float       sampleRate = assetlib::c_DefaultSampleRate;

	auto* bake = app.add_subcommand(
		"bake",
		"Import a .glb into a project: the mesh into Derived/Meshes/, its textures into "
		"Derived/SourceTextures/, the source copy and its import document into Authored/Meshes/, "
		"and the rig into Derived/Skeletons/ and Derived/Animations/ when it carries a skin");
	addProject(bake);
	bake->add_option("input", input, "Source .glb file, a path on disk")
		->required()
		->check(CLI::ExistingFile);
	bake->add_option(
		"-n,--name",
		name,
		"Base name for the imported assets (default: the source's stem)");
	bake->add_option(
			"-r,--sample-rate",
			sampleRate,
			"Hz every animation clip is resampled to (default: 30)")
		->check(CLI::PositiveNumber);

	std::string envInput;
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
		"Which level the written .benv presents (default: 0 = sharp). Reads as depth of field, and "
		"hides a source that cannot fill the face -- and unlike a baked blur it is reversible");
	envmap->add_option("-m,--mips", envMips, "Mip count; must match MAX_REFLECTION_LOD + 1");
	envmap->add_option("-n,--samples", envSamples, "GGX samples per texel (default: 128)");
	envmap->add_option(
		"-j,--threads",
		envThreads,
		"Worker threads (default: hardware concurrency)");

	std::string envName = "env";
	addProject(envmap);
	envmap->add_option("--name", envName, "Asset name for the written assets (default: env)");

	std::string objInput;
	std::string objOut;
	bool        objRaw = false;

	auto* obj = app.add_subcommand("obj", "Dump a .bmesh as a Wavefront .obj for inspection");
	addProject(obj);
	obj->add_option("input", objInput, "A .bmesh, relative to the data root")->required();
	obj->add_option(
		   "-o,--out",
		   objOut,
		   "Output .obj file, a path on disk -- an .obj is not a "
		   "project asset")
		->required();
	obj->add_flag(
		"--raw",
		objRaw,
		"Emit the raw index buffer instead of the meshlet-reconstructed geometry");

	std::string tangentsInput;

	auto* tangents = app.add_subcommand(
		"tangents",
		"Derive a tangent basis for every submesh of a .bmesh that has none, in place");
	addProject(tangents);
	tangents->add_option("input", tangentsInput, "A .bmesh, relative to the data root")->required();

	std::string describeInput;
	bool        describeBrief = false;

	auto* describe =
		app.add_subcommand("describe", "Print the contents of an asset container as text");
	addProject(describe);
	describe->add_option("input", describeInput, "A container, relative to the data root")
		->required();
	describe->add_flag(
		"-b,--brief",
		describeBrief,
		"Mesh only: print the summary and material table, but not every submesh");
	bool describeKey = false;
	describe->add_flag(
		"-k,--key",
		describeKey,
		"Also print the file's cache key: bake token, source, source stamp, parameter hash -- "
		"without loading a payload");

	std::string refsAsset;

	auto* refs = app.add_subcommand(
		"refs",
		"Print what references an asset, and whether it can therefore be deleted");
	addProject(refs);
	refs->add_option(
		"asset",
		refsAsset,
		"Asset to report on, relative to the data root. Omitted, the whole project is summarised, "
		"and every dangling reference listed");

	std::string renameFrom;
	std::string renameTo;

	auto* rename = app.add_subcommand(
		"rename",
		"Move an asset within a project and rewrite every reference that followed it");
	addProject(rename);
	rename->add_option("from", renameFrom, "Asset to move, relative to the data root")->required();
	rename->add_option("to", renameTo, "Where it lands, relative to the data root")->required();

	std::string pruneTextureDir = assetlib::c_BakedTexturesDirectoryName;
	bool        pruneDryRun     = false;
	bool        pruneYes        = false;

	auto* prune = app.add_subcommand(
		"prune",
		"Delete the baked textures under a project's data root that no material references any "
		"more");
	addProject(prune);
	prune->add_option(
		"-t,--texture-dir",
		pruneTextureDir,
		"Directory the material bake writes into, relative to the data root (default: Textures)");
	prune->add_flag("--dry-run", pruneDryRun, "List what would be deleted and delete nothing");
	prune->add_flag("-y,--yes", pruneYes, "Delete without asking for confirmation");

	bool bakeMaterialsDryRun = false;

	auto* bakeMaterials = app.add_subcommand(
		"bakematerials",
		"Composite every material in the project whose baked triplet is missing or stale down to "
		"the baseColor/normal/orm maps it draws from. The one derived output nothing else "
		"produces, so a checkout that ignores Textures/ opens untextured until this has run");
	addProject(bakeMaterials);
	bakeMaterials->add_flag(
		"--dry-run",
		bakeMaterialsDryRun,
		"List the materials that would be baked and write nothing");

	std::string packTarget;

	auto* pack = app.add_subcommand(
		"pack",
		"Write a project's data root into one .bpak archive of everything the runtime reads");
	addProject(pack);
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
	addProject(strip);
	strip->add_option("input", stripInput, "A .bmaterial, relative to the data root")->required();
	strip->add_option(
		"-o,--out",
		stripOut,
		"Write to this path on disk instead of rewriting the input -- a shipping tree is not a "
		"project. The routes and the node graph are not recoverable, so prefer this over stripping "
		"a material you still intend to author");
	strip->add_flag("-y,--yes", stripYes, "Rewrite the input without asking for confirmation");

	bool migrateDryRun = false;
	bool migrateYes    = false;

	auto* migrate = app.add_subcommand(
		"migrate",
		"Put every container the project's sources say should exist on disk, at what its current "
		"state says it should hold: an absent one is produced from the source that names it, "
		"stale geometry regenerates from its copied source, a rebind reaches its mesh, and a "
		"material whose sources no longer produce the maps it names is re-baked. A "
		"file that is already current is left untouched, so running it twice rewrites nothing "
		"the second time; a file that cannot be read -- or a stale group with no source -- is "
		"reported and skipped");
	addProject(migrate);
	migrate->add_flag(
		"-n,--dry-run",
		migrateDryRun,
		"Report what would be rewritten; write nothing");
	migrate->add_flag("-y,--yes", migrateYes, "Rewrite without asking for confirmation");

	bool reauthorYes = false;

	auto* reauthor = app.add_subcommand(
		"reauthor",
		"Rewrite every import document's bindings from its mesh's current state -- the one-time "
		"adoption step that makes the documents authoritative. Run it once when a project first "
		"picks up documents; run later it would overwrite any rebind saved only to a document "
		"with the mesh's older state");
	addProject(reauthor);
	reauthor->add_flag("-y,--yes", reauthorYes, "Rewrite without asking for confirmation");

	bool boundsDryRun = false;
	bool boundsYes    = false;

	auto* bakebounds = app.add_subcommand(
		"bakebounds",
		"Bake every rig's posed culling boxes into its .banim, measured against every .bmesh that "
		"names its skeleton -- the retrofit for a project imported before loads could read them. "
		"A clip set whose boxes are current is left untouched");
	addProject(bakebounds);
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
		"Show or author the exposure an environment renders at, overruling its bake's derivation");
	addProject(exposure);
	exposure->add_option("input", expInput, "A .benv, relative to the data root")->required();
	auto* expSetOpt = exposure->add_option(
		"-s,--set",
		expSet,
		"Author this exposure. Survives a re-bake, which refreshes only the derivation");
	exposure->add_flag("-c,--clear", expClear, "Drop the authored value and fall back to the bake")
		->excludes(expSetOpt);

	CLI11_PARSE(app, argc, argv);

	// After the parse, so --help does not print a memory report; before the work, so every early
	// return below is still covered.
	auto memoryReport = std::optional<core::profiling::MemoryReport>();
	if (!memReportPath.empty())
		memoryReport.emplace(memReportPath);

	if (*bake)
	{
		try
		{
			const assetlib::Project     project  = assetlib::Project::Open(projectFile);
			const std::filesystem::path dataRoot = project.GetDataDirectory();

			namespace fs = std::filesystem;

			if (name.empty())
				name = fs::path(input).stem().string();

			const fs::path bmeshPath = dataRoot / assetlib::c_MeshesDirectoryName /
			                           (name + std::string(assetlib::c_MeshExtension));
			const fs::path bskelPath =
				dataRoot / assetlib::c_SkeletonsDirectoryName / assetlib::skeletonFileName(name);
			const fs::path banimPath =
				dataRoot / assetlib::c_AnimationsDirectoryName / assetlib::animationFileName(name);

			// Its own folder: two sources naming an image alike would collide in a shared one.
			const fs::path textureDir = dataRoot / assetlib::c_SourceTexturesDirectoryName / name;

			assetlib::requireSelfContainedSource(input);

			const auto imported = assetlib::loadFromGltf(input, { .sampleRate = sampleRate });

			// Only what this import will actually write. WriteImportedRig no-ops on a source with no
			// skin, so a static mesh neither claims Skeletons/<name>.bskel nor may take one back
			// down: refusing over a file it never touches is the mild failure, deleting someone
			// else's on a rollback is not.
			const bool writesRig   = !imported.skeleton.bones.empty();
			const bool writesClips = writesRig && !imported.animations.clips.empty();

			auto files = std::vector<fs::path>{ bmeshPath };
			if (writesRig)
				files.push_back(bskelPath);
			if (writesClips)
				files.push_back(banimPath);

			// Import never overwrites, the same rule the editor's does: what it would replace is a
			// mesh someone authored materials against, and none of it is recoverable.
			auto            collisions = std::vector<std::string>();
			std::error_code ec;

			const auto noteIfPresent = [&](const fs::path& target) {
				if (fs::exists(target, ec))
					collisions.push_back(fs::relative(target, dataRoot, ec).generic_string());
			};

			const fs::path sourceCopy = assetlib::AssetStore(dataRoot).ImportedSourcePath(name);
			const fs::path importDoc  = assetlib::AssetStore(dataRoot).ImportDocumentPath(name);
			files.push_back(sourceCopy);
			files.push_back(importDoc);

			for (const fs::path& file : files) noteIfPresent(file);
			noteIfPresent(textureDir);

			if (!collisions.empty())
			{
				std::string named;
				for (const std::string& collision : collisions)
				{
					if (!named.empty())
						named += ", ";
					named += collision;
				}

				core::throw_runtime_error(
					"'{}' already holds {}. Import under another --name, or remove them first",
					project.GetName(),
					named);
			}

			// What to take back down if a later step throws. The refusal above means none of it was
			// there, so everything this writes is this import's to remove.
			auto written = std::vector<assetlib::ImportedFile>();
			written.reserve(files.size());
			for (const fs::path& file : files) written.push_back({ file, false });

			const std::array<assetlib::ImportedDir, 1> dirs = { {
				{ textureDir, false, dataRoot / assetlib::c_SourceTexturesDirectoryName },
			} };

			try
			{
				assetlib::BMesh mesh = assetlib::toBMesh(imported);
				assetlib::requireUniqueSubmeshNames(mesh);

				const assetlib::AssetStore importStore(dataRoot);
				assetlib::ImportTarget target{ name, sampleRate, importStore.KeyFor(textureDir) };
				const assetlib::SourceRef source = importStore.CopyImportedSource(input, target);
				mesh.source                      = source;

				importStore.WriteTextures(imported, target.textureDir);

				const auto derived = assetlib::generateTangents(mesh);

				auto outputs = importStore.WriteImportedRig(
					imported.skeleton,
					imported.animations,
					mesh,
					importStore.KeyFor(bskelPath),
					importStore.KeyFor(banimPath),
					true,
					source);
				importStore.Save(mesh, importStore.KeyFor(bmeshPath));

				outputs.push_back(importStore.KeyFor(bmeshPath));
				target.skeleton = mesh.skeleton;
				target.outputs  = std::move(outputs);
				importStore.WriteImportedDocument(target, &mesh);

				if (derived.skipped > 0)
					spdlog::warn(
						"{} submesh(es) have no tangent and no way to derive one (no normals, no "
						"UVs, or no triangles) -- a normal map on those will not render",
						derived.skipped);

				spdlog::info(
					"Imported '{}' into '{}': {}, {} texture(s) -> {}/",
					input,
					project.GetName(),
					fs::relative(bmeshPath, dataRoot, ec).generic_string(),
					imported.textures.size(),
					fs::relative(textureDir, dataRoot, ec).generic_string());

				if (!imported.skeleton.bones.empty())
					spdlog::info(
						"  rig -> {} ({} bones, {} clips at {} Hz)",
						fs::relative(bskelPath, dataRoot, ec).generic_string(),
						imported.skeleton.bones.size(),
						imported.animations.clips.size(),
						sampleRate);

				// Nothing here writes a material: a glTF's are PBR, which is that format's shading
				// model rather than necessarily the engine's, and the board that decides what a
				// material routes where lives in the editor. The textures land for one to be
				// authored against.
				if (!imported.materials.empty())
					spdlog::info(
						"  {} source material(s) left unassigned -- author them in the editor",
						imported.materials.size());
			}
			catch (...)
			{
				assetlib::rollBackImport(written, dirs);
				throw;
			}
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
			const assetlib::Project project = assetlib::Project::Open(projectFile);

			auto importDesc               = assetlib::EnvImportDesc();
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

			const assetlib::EnvImportResult imported =
				project.GetStore().ImportEnvironment(importDesc);

			spdlog::info(
				"Imported '{}' into '{}': {} files, exposure {:.3f}",
				envName,
				project.GetName(),
				imported.written.size(),
				imported.exposure);

			for (const std::string& file : imported.written) spdlog::info("  wrote {}", file);
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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const auto mesh = store.Load<assetlib::BMesh>(assetlib::normalizePath(objInput));
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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const std::string key = assetlib::normalizePath(tangentsInput);

			assetlib::BMesh mesh   = store.Load<assetlib::BMesh>(key);
			const auto      result = assetlib::generateTangents(mesh);

			if (result.generated > 0)
				store.Save(mesh, key);

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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const std::string key = assetlib::normalizePath(describeInput);

			// Every container through the store: it stats each routed source against what is on
			// disk, so a stale bake is reported rather than merely recorded. The three that route
			// nothing answer here too, so this switch never has to know which kind it is holding.
			const auto describeAsset = [&store](const auto&... asset) {
				return store.Describe(asset...);
			};

			if (describeKey)
			{
				const std::vector<std::byte> bytes = store.GetFiles().Read(key);
				// Straight to stdout, not the logger: this is the command's output, so it should
				// pipe into a file or a diff without spdlog's timestamps and level prefixes.
				if (assetlib::isTextAssetDocument(bytes))
				{
					// An authored text document has no cache key; it is its own description.
					std::cout << "authored text document\n";
				}
				else if (const auto entry = assetlib::inspectCacheEntry(bytes))
				{
					std::cout << std::format(
						"cache entry\n  bake token   {:#018x}\n  source       {}\n  source "
						"stamp  {} bytes, {:#018x}\n  parameters   {:#018x}\n",
						entry->bakeToken,
						entry->source.key.empty() ? "(never recorded)" : entry->source.key,
						entry->source.stamp.size,
						entry->source.stamp.hash,
						entry->source.parametersHash);
				}
				else
				{
					core::throw_runtime_error(
						"{} is neither a text document nor a cache entry; a chunk-era file is "
						"no longer convertible -- re-import or re-author it",
						key);
				}
			}

			switch (sniff(store, key))
			{
			case assetlib::AssetType::kMesh:
				std::cout << describeAsset(store.Load<assetlib::BMesh>(key), !describeBrief);
				break;
			case assetlib::AssetType::kMaterial:
				std::cout << describeAsset(store.Load<assetlib::BMaterial>(key));
				break;
			case assetlib::AssetType::kEnvironment:
				std::cout << describeAsset(store.Load<assetlib::BEnv>(key));
				break;
			case assetlib::AssetType::kSky:
				std::cout << describeAsset(store.Load<assetlib::BSky>(key));
				break;
			case assetlib::AssetType::kEnvLighting:
				std::cout << describeAsset(store.Load<assetlib::BEnvLighting>(key));
				break;
			case assetlib::AssetType::kSkeleton:
				std::cout << describeAsset(store.Load<assetlib::Skeleton>(key));
				break;
			case assetlib::AssetType::kAnimation:
			{
				const auto animations = store.Load<assetlib::AnimationSet>(key);
				const auto skeleton   = resolveSkeleton(store, animations.skeleton);
				std::cout << describeAsset(animations, skeleton ? &*skeleton : nullptr);
				break;
			}
			case assetlib::AssetType::kAvatar:
			{
				const auto avatar = store.Load<assetlib::Avatar>(key);

				// Found by the key rather than by anything the avatar names, which is the whole of
				// the convention -- and a rig that is not there is exactly what makes the names
				// worth checking, so an absent one prints the avatar bare rather than failing.
				const auto skeleton = resolveSkeleton(store, assetlib::skeletonKeyForAvatar(key));
				std::cout << describeAsset(avatar, skeleton ? &*skeleton : nullptr);
				break;
			}
			// sniff never answers either: a foreign kind has no codec, and an import document is
			// text whose extension the text branch does not accept. Listed so the switch stays
			// exhaustive, which is what makes a new AssetType a compile error here.
			case assetlib::AssetType::kTexture:
			case assetlib::AssetType::kImportDocument:
			case assetlib::AssetType::kUiDocument:
			case assetlib::AssetType::kUiStyle:
			case assetlib::AssetType::kFont:
			case assetlib::AssetType::kCount:
				core::throw_runtime_error("{} is not a container describe can read", key);
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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const std::string           key = assetlib::normalizePath(stripInput);
			const std::filesystem::path in  = store.ResolveWritePath(key);
			const std::filesystem::path out =
				stripOut.empty() ? in : std::filesystem::path(stripOut);

			assetlib::BMaterial material = store.Load<assetlib::BMaterial>(key);

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
			// Bytes to a host path, not a project write: --out names a shipping tree, which no store
			// owns. The encode is the codec's; where it lands is the caller's.
			core::file::write_atomic(
				out,
				assetlib::AssetCodec<assetlib::BMaterial>::Serialize(material));

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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const std::filesystem::path root    = project.GetDataDirectory();

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
				for (const assetlib::MovedTexture& moved : report.movedTextures)
					std::cout << "followed: " << moved.from << " -> " << moved.to
							  << " (same bytes; the materials routing at it were rewritten)\n";

				for (const std::string& dangling : report.danglingTextures)
					std::cout << "material names a texture that is not there: " << dangling << '\n';

				// Named and left where they are -- see docs/asset_containers.md.
				for (const std::string& superseded : report.supersededTextures)
					std::cout << "no longer extracted, still on disk: " << superseded << '\n';

				std::cout << std::format(
					"{} unchanged, {} {}, {} cannot be converted\n",
					report.Count(assetlib::MigratedFile::Outcome::kUnchanged),
					report.Count(assetlib::MigratedFile::Outcome::kRewritten),
					preview ? "to rewrite" : "rewritten",
					report.Count(assetlib::MigratedFile::Outcome::kFailed));
			};

			// The preview walk never writes, so a read-only file or a full disk shows up only in
			// the real one -- which is why the real walk's report is the one printed last. With
			// -y there is nobody to show it to, and a preview now costs real work: a stale group
			// re-imports its source once per file, so the confirmed path pays that once, not
			// twice.
			if (migrateDryRun || !migrateYes)
			{
				const auto preview = assetlib::AssetStore(root).Migrate(true);
				print(preview, true);
				if (migrateDryRun)
					return preview.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;

				const auto toRewrite = preview.Count(assetlib::MigratedFile::Outcome::kRewritten);
				if (toRewrite == 0)
					return preview.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;
				if (!confirm(std::format("Rewrite {} file(s) in place?", toRewrite)))
				{
					spdlog::info("Left '{}' alone.", root.string());
					return 0;
				}
			}

			const auto report = assetlib::AssetStore(root).Migrate(false);
			print(report, false);
			return report.Count(assetlib::MigratedFile::Outcome::kFailed) == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("migrate failed: {}", e.what());
			return 1;
		}
	}

	if (*reauthor)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const std::filesystem::path root    = project.GetDataDirectory();

			if (!reauthorYes &&
			    !confirm("Rewrite the import documents' bindings from the meshes in place?"))
			{
				spdlog::info("Left '{}' alone.", root.string());
				return 0;
			}

			const std::vector<assetlib::ReauthoredDocument> report =
				assetlib::AssetStore(root).ReauthorImportDocuments();

			size_t rewritten = 0;
			size_t failed    = 0;
			for (const assetlib::ReauthoredDocument& document : report)
			{
				switch (document.outcome)
				{
				case assetlib::ReauthoredDocument::Outcome::kUnchanged:
					break;
				case assetlib::ReauthoredDocument::Outcome::kRewritten:
					++rewritten;
					std::cout << "reauthored      " << document.key << '\n';
					break;
				case assetlib::ReauthoredDocument::Outcome::kFailed:
					++failed;
					std::cout << "cannot reauthor " << document.key << ": " << document.message
							  << '\n';
					break;
				}
			}
			std::cout << std::format(
				"{} unchanged, {} reauthored, {} failed\n",
				report.size() - rewritten - failed,
				rewritten,
				failed);
			return failed == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("reauthor failed: {}", e.what());
			return 1;
		}
	}

	if (*bakebounds)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const std::filesystem::path root    = project.GetDataDirectory();

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
			const auto preview = assetlib::AssetStore(root).RebakePosedBounds(true);
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

			const auto report = assetlib::AssetStore(root).RebakePosedBounds(false);
			print(report, false);
			return report.Count(assetlib::RebakedFile::Outcome::kFailed) == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("bakebounds failed: {}", e.what());
			return 1;
		}
	}

	if (*rename)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const auto plan =
				assetlib::planRename(assetlib::AssetRefGraph::Scan(store), renameFrom, renameTo);

			const assetlib::RenameResult result = store.RenameAsset(plan);
			if (result.status != assetlib::RenameStatus::kRenamed)
			{
				spdlog::error("rename failed: {}", result.error);
				return 1;
			}

			spdlog::info(
				"Renamed {} -> {}, rewriting {} reference(s)",
				plan.subject.from,
				plan.subject.to,
				plan.referrers.size());
			return 0;
		}
		catch (const std::exception& e)
		{
			spdlog::error("rename failed: {}", e.what());
			return 1;
		}
	}

	if (*refs)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const auto graph = assetlib::AssetRefGraph::Scan(store);

			spdlog::info(
				"Scanned {} meshes, {} materials, {} environment assets, {} clip sets "
				"and {} import documents: {} references",
				graph.meshesScanned,
				graph.materialsScanned,
				graph.environmentsScanned,
				graph.clipSetsScanned,
				graph.importDocumentsScanned,
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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			// Beside the project file, not inside Data/: the archive is what the project produces,
			// and packing it into the tree it was packed from would pack itself next time.
			auto desc   = assetlib::PackDesc();
			desc.target = packTarget.empty() ? project.GetProjectFile().parent_path() /
			                                       assetlib::c_DefaultArchiveName :
			                                   std::filesystem::path(packTarget);

			const assetlib::PackReport report = store.Pack(desc);

			if (report.geometryRebaked != 0)
				spdlog::info(
					"Re-baked {} geometry entr{} into the archive (stale on disk, or a rebind "
					"not yet migrated)",
					report.geometryRebaked,
					report.geometryRebaked == 1 ? "y" : "ies");

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

	if (*bakeMaterials)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			size_t baked   = 0;
			size_t current = 0;
			size_t failed  = 0;

			for (const std::string& key : store.GetFiles().Enumerate(""))
			{
				if (!key.ends_with(assetlib::c_MaterialExtension))
					continue;

				try
				{
					assetlib::BMaterial material = store.Load<assetlib::BMaterial>(key);
					if (!store.BakeIsStale(material))
					{
						++current;
						continue;
					}

					std::cout << (bakeMaterialsDryRun ? "would bake " : "baked ") << key << '\n';
					if (!bakeMaterialsDryRun)
					{
						store.BakeMaterial(material);
						store.Save(material, key);
					}
					++baked;
				}
				catch (const std::exception& e)
				{
					std::cout << "cannot bake " << key << ": " << e.what() << '\n';
					++failed;
				}
			}

			std::cout << std::format(
				"{} current, {} {}, {} failed\n",
				current,
				baked,
				bakeMaterialsDryRun ? "to bake" : "baked",
				failed);
			return failed == 0 ? 0 : 1;
		}
		catch (const std::exception& e)
		{
			spdlog::error("{}", e.what());
			return 1;
		}
	}

	if (*prune)
	{
		try
		{
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			auto desc       = assetlib::TexturePruneDesc();
			desc.textureDir = pruneTextureDir;

			const auto scan = store.FindUnusedBakedTextures(desc);

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
			std::cout << "Unused (" << scan.unused.size() << ", "
					  << core::str::format_bytes(scan.bytes) << "):\n";
			for (const assetlib::UnusedTexture& texture : scan.unused)
				std::cout << "  " << texture.path << "  (" << core::str::format_bytes(texture.bytes)
						  << ")\n";
			std::cout << std::flush;

			if (pruneDryRun)
			{
				spdlog::info("Dry run: nothing deleted. Re-run without --dry-run to delete.");
				return 0;
			}

			if (!pruneYes &&
			    !confirm(
					"Delete " + std::to_string(scan.unused.size()) + " unused baked textures (" +
					core::str::format_bytes(scan.bytes) + ")?"))
			{
				spdlog::info("Cancelled: nothing deleted.");
				return 0;
			}

			const auto result = store.DeleteUnusedBakedTextures(scan);

			spdlog::info(
				"Deleted {} textures, reclaiming {}",
				result.deleted,
				core::str::format_bytes(result.bytes));

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
			const assetlib::Project     project = assetlib::Project::Open(projectFile);
			const assetlib::AssetStore& store   = project.GetStore();

			const std::string key = assetlib::normalizePath(expInput);
			assetlib::BEnv    env = store.Load<assetlib::BEnv>(key);

			// Read before the write, so a lighting that cannot be loaded refuses before the
			// document changes rather than after.
			const assetlib::BEnvLighting lighting =
				env.lighting.empty() ? assetlib::BEnvLighting() :
									   store.Load<assetlib::BEnvLighting>(env.lighting);

			if (*expSetOpt || expClear)
			{
				if (expClear)
					env.exposureOverride.reset();
				else
					env.exposureOverride = expSet;

				store.Save(env, key);
			}

			spdlog::info(
				"'{}': derived {:.6g}, authored {}, rendering at {:.6g}",
				expInput,
				lighting.exposure,
				env.exposureOverride ? std::format("{:.6g}", *env.exposureOverride) :
									   std::string("(none)"),
				assetlib::effectiveExposure(env, lighting));
		}
		catch (const std::exception& e)
		{
			spdlog::error("exposure failed: {}", e.what());
			return 1;
		}
	}

	return 0;
}
