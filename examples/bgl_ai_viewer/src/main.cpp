#define NOMINMAX

#include <CLI/CLI.hpp>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/bmesh.h>
#include <assetlib/grass_patch.h>
#include <assetlib/import_document.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BGrass.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PassTiming.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SkinnedMeshInstanceDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <bgl/types/Viewport.h>
#include <bgl/types/WindDesc.h>
#include <cmath>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <gamelib/AssetManager.h>
#include <gamelib/ClipInfo.h>
#include <gamelib/PassHistory.h>
#include <gamelib/pass_timing_csv.h>
#include <glm/gtc/matrix_transform.hpp>
#include <headless/PassCosts.h>
#include <headless/framing.h>
#include <headless/headless_render.h>
#include <headless/import_lookup.h>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// A model rendered headlessly for an agent to look at: PNGs at the frames it names, a clip played on
// a fixed clock so frame N is the same pose every run, and what the GPU spent on each pass.
// docs/ai_viewer.md is the manual.

namespace
{
	struct Options
	{
		std::string project = "assets/Data";
		std::string import  = "Authored/Meshes/apples.bimport";
		std::string clip;
		std::string env = "Authored/Environments/forest.benv";
		std::string envRoot;
		std::string outDir = "ai_viewer";

		std::vector<uint32_t> screenshots;

		uint32_t width       = 1280;
		uint32_t height      = 720;
		uint32_t frames      = 60;
		uint32_t warmup      = 8;
		float    fps         = 30.0f;
		bool     taa         = true;
		float    renderScale = 1.0f;

		// Off unless asked for, as bgl's own default is; on, it takes bgl's default settings.
		bool bloom = false;

		// The post-process, "filmic" or "toon".
		std::string postProcessType = "filmic";

		// The camera frames the box every clip's poses fill unless asked for the playing clip's
		// alone: a clip set with root motion walks that box far past any one pose.
		bool frameClip = false;

		// The sun is off unless asked for, as bgl's own default is: every render this tool made
		// before there was one stays the render it made.
		float              sunAzimuth   = 35.0f;
		float              sunElevation = 38.0f;
		float              sunIntensity = 0.0f;
		std::vector<float> sunColor{ 1.0f, 1.0f, 1.0f };

		// A `.bgrass` grown on a patch of bare ground, in place of --import. Size 0 is twice the
		// look's fade end.
		std::string grass;
		float       patchSize    = 0.0f;
		float       patchSpacing = 0.25f;
		float       distance     = 10.0f;
		float       wind         = 0.0f;

		// Copies of the model laid out in rows receding from the camera, `crowdColumns` a row, each a
		// fraction of a cycle out of step with the one before. 0 places the model once.
		uint32_t crowd        = 0;
		uint32_t crowdColumns = 6;

		// The pose source a skinned mesh is spawned on: per-instance, table or auto. Empty is
		// per-instance for a single model and auto for a crowd.
		std::string source;

		// The view's selection of the automatic source, LodSelectionDesc's defaults unless asked.
		std::optional<uint32_t> poseBudget;
		std::optional<float>    posePixels;

		// The level every placement draws (LodSelectionDesc::forceLevel). Empty selects by size.
		std::optional<uint32_t> lod;
	};

	/** @throws std::runtime_error naming the three spellings when `name` is none of them. */
	[[nodiscard]] bgl::PoseSource
	ParseSource(const std::string_view name, const bool crowd)
	{
		static const auto c_Sources = std::map<std::string_view, bgl::PoseSource>{
			{ "per-instance", bgl::PoseSource::kPerInstance },
			{ "table", bgl::PoseSource::kBoneAnimTable },
			{ "auto", bgl::PoseSource::kAuto },
		};
		if (name.empty())
			return crowd ? bgl::PoseSource::kAuto : bgl::PoseSource::kPerInstance;

		if (const auto it = c_Sources.find(name); it != c_Sources.end())
			return it->second;
		core::throw_runtime_error("--source {} is none of per-instance, table and auto", name);
	}

	[[nodiscard]] std::string_view
	SourceName(const bgl::PoseSource source) noexcept
	{
		switch (source)
		{
		case bgl::PoseSource::kBoneAnimTable:
			return "table";
		case bgl::PoseSource::kAuto:
			return "auto";
		case bgl::PoseSource::kPerInstance:
			break;
		}
		return "per-instance";
	}

	/**
	 * Where crowd unit `unit` stands: rows of `columns` across X, receding along -Z, `spacing` apart.
	 * Row 0 is nearest the camera CrowdCamera places.
	 */
	[[nodiscard]] glm::mat4
	CrowdOffset(const uint32_t unit, const uint32_t columns, const float spacing)
	{
		const auto column = static_cast<float>(unit % columns);
		const auto row    = static_cast<float>(unit / columns);
		const auto across = (column - 0.5f * static_cast<float>(columns - 1)) * spacing;
		return glm::translate(glm::mat4(1.0f), glm::vec3(across, 0.0f, -row * spacing));
	}

	/**
	 * A camera at the crowd's near end, a unit's height up and two and a half spacings back, looking
	 * down the rows:
	 * the front units fill much of the frame and the back ones a few pixels, which is the range the
	 * automatic source chooses across.
	 */
	[[nodiscard]] bgl::Camera
	CrowdCamera(
		const Options&          opts,
		const assetlib::Bounds& unit,
		const float             spacing,
		const uint32_t          rows)
	{
		const float height = std::max(unit.max.y - unit.min.y, 0.01f);
		const float depth  = static_cast<float>(rows) * spacing;

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, unit.min.y + 1.2f * height, 2.5f * spacing),
				glm::vec3(0.0f, unit.min.y + 0.4f * height, -0.5f * depth),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(opts.width) / static_cast<float>(opts.height),
				0.05f * spacing,
				2.0f * (depth + spacing));
		return camera;
	}

	// Where a patch's camera stands above its ground.
	constexpr float c_EyeHeight = 1.6f;

	// Timed frames drawn past the last one, held at its time, to collect rows that trail their frame.
	constexpr uint32_t c_DrainFrames = 16;

	void
	PrintClips(const std::vector<game::ClipInfo>& clips, const uint32_t playing)
	{
		std::cout << "clips\n";
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			const game::ClipInfo& clip = clips[i];
			std::cout << std::format(
				"  {} {:<32} {:>7.3f} s  {:>5} frames at {} Hz{}\n",
				i == playing ? '>' : ' ',
				clip.name,
				clip.duration,
				clip.frameCount,
				clip.sampleRate,
				clip.loop ? ", loops" : "");
		}
	}

	/**
	 * Places every mesh entry of `opts.import`'s `.bmesh`, a skinned one playing its clip, and
	 * returns the camera framing them. Grass the mesh grows comes with it (AssetManager::AcquireMesh).
	 *
	 * @throws std::runtime_error when the import or a container it names is missing, or `--clip`
	 *         names nothing.
	 */
	[[nodiscard]] bgl::Camera
	PlaceImport(
		const Options&               opts,
		const assetlib::AssetStore&  store,
		const std::filesystem::path& dataRoot,
		game::AssetManager&          assets,
		const bgl::SceneViewRef&     view)
	{
		const std::string documentKey = headless::ImportDocumentKey(opts.import);
		if (!store.Exists(documentKey))
		{
			core::throw_runtime_error(
				"{} is not in {}",
				documentKey,
				std::filesystem::absolute(dataRoot).string());
		}

		const assetlib::ImportDocument document =
			assetlib::loadImportDocument(store.GetFiles(), documentKey);

		const std::string meshKey = document.GetMeshOutput();
		if (meshKey.empty())
		{
			core::throw_runtime_error("{} produced no .bmesh", documentKey);
		}
		headless::RequireDerived(store, meshKey, dataRoot);

		const std::string animationsKey = headless::AnimationOutput(document);
		const bool        rigged        = !animationsKey.empty();
		if (!rigged && !opts.clip.empty())
		{
			core::throw_runtime_error(
				"--clip {}: {} has no clip set, so there is nothing to play",
				opts.clip,
				documentKey);
		}

		const auto model = store.Load<assetlib::BMesh>(meshKey);

		std::vector<std::optional<assetlib::Bounds>> posedBounds;
		std::optional<assetlib::AnimationSet>        animations;
		std::optional<assetlib::Skeleton>            skeleton;
		if (rigged)
		{
			headless::RequireDerived(store, animationsKey, dataRoot);
			animations = store.Load<assetlib::AnimationSet>(animationsKey);
			headless::RequireDerived(store, animations->skeleton, dataRoot);
			skeleton    = store.Load<assetlib::Skeleton>(animations->skeleton);
			posedBounds = assetlib::findPosedBounds(*animations, model, *skeleton);
		}

		struct SkinnedPlacement
		{
			bgl::GeomHandle geom;
			glm::mat4       world;
			uint32_t        meshIndex;
		};

		auto                          bounds = headless::EmptyBounds();
		std::vector<SkinnedPlacement> skinned;
		std::vector<game::ClipInfo>   clips;
		for (uint32_t n = 0; n < model.nodes.size(); ++n)
		{
			const uint32_t meshIndex = model.nodes[n].mesh;
			if (meshIndex == assetlib::c_InvalidIndex)
				continue;

			const glm::mat4 world = headless::InstanceTransform(model, n);
			if (!rigged || !assetlib::isSkinned(model, meshIndex))
			{
				assets.CreateInstance(
					view,
					bgl::StaticMeshInstanceDesc()
						.SetGeom(assets.AcquireMesh(document.source, meshIndex))
						.SetTransform(world));
				headless::GrowBounds(bounds, world, headless::MeshEntryBounds(model, meshIndex));
				continue;
			}

			// The box the geom culls by is also the camera's frame, so it is resolved here once for both.
			const assetlib::Bounds posed =
				posedBounds[meshIndex] ?
					*posedBounds[meshIndex] :
					assetlib::posedBounds(model, meshIndex, *skeleton, *animations);

			game::AssetManager::SkinnedMesh acquired =
				assets.AcquireSkinnedMesh(document.source, document.source, {}, meshIndex, posed);
			skinned.emplace_back(acquired.geom, world, meshIndex);
			clips = std::move(acquired.clips);
			headless::GrowBounds(bounds, world, posed);
		}

		const uint32_t clip = rigged && !skinned.empty() ? headless::FindClip(clips, opts.clip) : 0;

		const bool            crowd  = opts.crowd > 0;
		const bgl::PoseSource source = ParseSource(opts.source, crowd);

		// The culling box stays the whole clip set's; only the camera narrows to the one clip, measured
		// the same way over a set holding that clip alone. A crowd is spaced by it too: a clip set
		// whose clips travel would otherwise space units by the distance they walk.
		if ((opts.frameClip || crowd) && !skinned.empty())
		{
			assetlib::AnimationSet playing = *animations;
			playing.clips                  = { animations->clips.at(clip) };
			playing.posedBoxes.clear();

			bounds = headless::EmptyBounds();
			for (uint32_t n = 0; n < model.nodes.size(); ++n)
			{
				const uint32_t meshIndex = model.nodes[n].mesh;
				if (meshIndex != assetlib::c_InvalidIndex && !assetlib::isSkinned(model, meshIndex))
					headless::GrowBounds(
						bounds,
						headless::InstanceTransform(model, n),
						headless::MeshEntryBounds(model, meshIndex));
			}
			for (const SkinnedPlacement& placement : skinned)
				headless::GrowBounds(
					bounds,
					placement.world,
					assetlib::posedBounds(model, placement.meshIndex, *skeleton, playing));
		}
		const float spacing =
			1.5f * std::max({ bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z, 0.01f });
		const uint32_t rows = crowd ? (opts.crowd + opts.crowdColumns - 1) / opts.crowdColumns : 0;

		if (!crowd)
		{
			for (const SkinnedPlacement& placement : skinned)
			{
				assets.CreateSkinnedInstance(
					view,
					bgl::SkinnedMeshInstanceDesc()
						.SetGeom(placement.geom)
						.SetTransform(placement.world)
						.SetPlayback(bgl::SkinnedPlaybackDesc::FromClip(clip))
						.SetSource(source));
			}
		}
		else
		{
			// The first copy is the one already placed above; the static parts follow every other.
			const float frames = rigged && !skinned.empty() ?
			                         static_cast<float>(animations->clips.at(clip).frameCount) :
			                         1.0f;

			for (uint32_t unit = 0; unit < opts.crowd; ++unit)
			{
				const glm::mat4 offset = CrowdOffset(unit, opts.crowdColumns, spacing);
				if (unit > 0)
				{
					for (uint32_t n = 0; n < model.nodes.size(); ++n)
					{
						const uint32_t meshIndex = model.nodes[n].mesh;
						if (meshIndex != assetlib::c_InvalidIndex &&
						    (!rigged || !assetlib::isSkinned(model, meshIndex)))
							assets.CreateInstance(
								view,
								bgl::StaticMeshInstanceDesc()
									.SetGeom(assets.AcquireMesh(document.source, meshIndex))
									.SetTransform(offset * headless::InstanceTransform(model, n)));
					}
				}

				// The golden ratio's fraction of a cycle per unit, so no two neighbours step together
				// and the whole crowd never falls back into phase.
				const float phase = std::fmod(static_cast<float>(unit) * 0.618034f, 1.0f) * frames;
				for (const SkinnedPlacement& placement : skinned)
				{
					assets.CreateSkinnedInstance(
						view,
						bgl::SkinnedMeshInstanceDesc()
							.SetGeom(placement.geom)
							.SetTransform(offset * placement.world)
							.SetPlayback(bgl::SkinnedPlaybackDesc::FromClip(clip, phase))
							.SetSource(source));
				}
			}

			std::cout << std::format(
				"crowd  {} units, {} a row over {} rows, {:.2f} apart\n",
				opts.crowd,
				opts.crowdColumns,
				rows,
				spacing);
		}

		std::cout << std::format(
			"{}\nmesh   {} ({})\n",
			documentKey,
			meshKey,
			skinned.empty() ?
				"static" :
				std::format("skinned, {}, posed {}", animationsKey, SourceName(source)));
		if (!skinned.empty())
			PrintClips(clips, clip);

		if (crowd)
			return CrowdCamera(opts, bounds, spacing, rows);
		return headless::FrameBounds(bounds, opts.width, opts.height);
	}

	/**
	 * Grows `opts.grass` on a patch of bare ground and returns a camera standing in it, eye height
	 * above the ground and `opts.distance` from the patch's centre, looking at it.
	 *
	 * @throws std::runtime_error when the look is not a `.bgrass` in the project, or what
	 *         AssetManager::CreateGrassPatch throws.
	 */
	[[nodiscard]] bgl::Camera
	PlaceGrassPatch(
		const Options&              opts,
		const assetlib::AssetStore& store,
		bgl::IScene&                scene,
		game::AssetManager&         assets,
		const bgl::SceneViewRef&    view)
	{
		if (!opts.grass.ends_with(".bgrass") || !store.Exists(opts.grass))
		{
			core::throw_runtime_error("--grass {} names no .bgrass in the project", opts.grass);
		}

		const auto look = store.Load<assetlib::BGrass>(opts.grass);

		// Twice the fade end unless asked, so a camera anywhere in the fade sees grass to its end.
		auto patch    = assetlib::GrassPatchDesc();
		patch.size    = opts.patchSize > 0.0f ? opts.patchSize : 2.0f * look.density.fadeEnd;
		patch.spacing = opts.patchSpacing;

		const bgl::MaterialHandle ground = scene.CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.22f, 0.19f, 0.15f, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
		assets.CreateInstance(
			view,
			bgl::StaticMeshInstanceDesc()
				.SetGeom(assets.CreateGrassPatch(patch, opts.grass, ground))
				.SetTransform(
					glm::rotate(
						glm::mat4(1.0f),
						glm::radians(-90.0f),
						glm::vec3(1.0f, 0.0f, 0.0f))));

		if (opts.wind > 0.0f)
		{
			view->SetWind(
				{ .direction    = glm::vec3(1.0f, 0.0f, 0.4f),
			      .strength     = opts.wind,
			      .gustStrength = opts.wind });
		}

		std::cout << std::format(
			"{}\npatch  {:.0f} m square, a clump every {} m, camera {} m out\nwind   {}\n",
			opts.grass,
			patch.size,
			patch.spacing,
			opts.distance,
			opts.wind > 0.0f ? std::format("{} with gusts", opts.wind) : std::string("calm"));

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, c_EyeHeight, opts.distance),
				glm::vec3(0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(opts.width) / static_cast<float>(opts.height),
				0.05f,
				2.0f * patch.size);
		return camera;
	}
}

int
main(int argc, char** argv)
try
{
	Options opts;
	{
		CLI::App app{ "Render a mesh headlessly: PNGs at chosen frames, and per-pass GPU cost" };
		app.set_help_flag("--help", "Print this help message and exit");
		app.add_option(
			"--project",
			opts.project,
			"The project's Data directory, absolute: every key below is relative to it");
		auto* import = app.add_option(
			"--import",
			opts.import,
			"The .bimport to render, or the .glb it describes; its outputs name the .bmesh and, "
			"for a rig, the .banim");
		app.add_option(
			"--clip",
			opts.clip,
			"The clip a skinned mesh plays, by name; defaults to the first");
		app.add_option(
			   "--screenshot",
			   opts.screenshots,
			   "Frames to write as PNGs, comma-separated, each below --frames")
			->delimiter(',');
		app.add_option("--out-dir", opts.outDir, "Where the PNGs and gpu_timings.csv are written");
		app.add_option("--frames", opts.frames, "Frames to render and time")
			->check(CLI::PositiveNumber);
		app.add_option("--fps", opts.fps, "Frame i renders at clip time i / fps")
			->check(CLI::PositiveNumber);
		app.add_option(
			   "--warmup",
			   opts.warmup,
			   "Frames rendered first at time 0, before frame 0, and neither timed nor numbered")
			->check(CLI::NonNegativeNumber);
		app.add_option("--env", opts.env, "The .benv to light it with");
		app.add_option(
			"--env-root",
			opts.envRoot,
			"Data root the .benv is keyed under; defaults to --project");
		app.add_option("-w,--width", opts.width, "Render width")->check(CLI::PositiveNumber);
		app.add_option("-h,--height", opts.height, "Render height")->check(CLI::PositiveNumber);
		app.add_option("--taa", opts.taa, "Render with temporal antialiasing, as a viewport does");
		app.add_option(
			   "--render-scale",
			   opts.renderScale,
			   "The geometry passes' grid relative to the output size; below 1 the TAA resolve "
			   "reconstructs the output (RenderTargetDesc::renderScale)")
			->check(CLI::PositiveNumber);
		app.add_flag("--bloom", opts.bloom, "Render with bloom at bgl's default settings");
		app.add_option(
			   "--post-process",
			   opts.postProcessType,
			   "The post-process: filmic (bgl's default, AgX and the grade) or toon, the exposed "
			   "colour clamped, which a toon look is authored for "
			   "(IRenderTarget::SetPostProcessType)")
			->check(CLI::IsMember({ "filmic", "toon" }));
		app.add_flag(
			"--frame-clip",
			opts.frameClip,
			"Frame the camera on the playing clip's poses rather than every clip's");
		app.add_option(
			   "--sun",
			   opts.sunIntensity,
			   "Sun intensity, in the irradiance map's units. 0 leaves it off")
			->check(CLI::NonNegativeNumber);
		app.add_option(
			"--sun-azimuth",
			opts.sunAzimuth,
			"Sun azimuth in degrees about the up axis");
		app.add_option("--sun-elevation", opts.sunElevation, "Sun elevation in degrees");
		app.add_option("--sun-color", opts.sunColor, "Sun colour as three floats")->expected(3);
		app.add_option(
			   "--grass",
			   opts.grass,
			   "A .bgrass to grow on a patch of bare ground, in place of --import")
			->excludes(import);
		app.add_option(
			   "--patch-size",
			   opts.patchSize,
			   "The patch's side in metres; 0 is twice the look's fade end")
			->check(CLI::NonNegativeNumber);
		app.add_option("--patch-spacing", opts.patchSpacing, "Metres between the patch's clumps")
			->check(CLI::PositiveNumber);
		app.add_option(
			   "--distance",
			   opts.distance,
			   "How far from the patch's centre its camera stands, in metres")
			->check(CLI::NonNegativeNumber);
		app.add_option(
			   "--wind",
			   opts.wind,
			   "The patch's wind: steady strength and gust strength both, in [0, 1]; 0 is calm")
			->check(CLI::Range(0.0f, 1.0f));
		app.add_option(
			   "--crowd",
			   opts.crowd,
			   "Copies of the model in rows receding from the camera, phases staggered; 0 is one")
			->check(CLI::NonNegativeNumber);
		app.add_option("--crowd-columns", opts.crowdColumns, "Crowd units a row")
			->check(CLI::PositiveNumber);
		app.add_option(
			"--source",
			opts.source,
			"The pose source a skinned mesh is spawned on: per-instance, table or auto; "
			"per-instance for one model and auto for a crowd");
		app.add_option(
			   "--pose-budget",
			   opts.poseBudget,
			   "Automatic units the view poses per instance at once (LodSelectionDesc::poseBudget)")
			->check(CLI::NonNegativeNumber);
		app.add_option(
			   "--pose-pixels",
			   opts.posePixels,
			   "Size on screen below which an automatic unit on a one-level mesh draws from its "
			   "table (LodSelectionDesc::posePixels)")
			->check(CLI::PositiveNumber);
		app.add_option(
			   "--lod",
			   opts.lod,
			   "The level every placement draws, whatever its size; a mesh with fewer draws its "
			   "coarsest (LodSelectionDesc::forceLevel)")
			->check(CLI::Range(0u, bgl::cMaxMeshLods - 1u));

		CLI11_PARSE(app, argc, argv);
	}

	std::ranges::sort(opts.screenshots);
	const auto [duplicates, end] = std::ranges::unique(opts.screenshots);
	opts.screenshots.erase(duplicates, end);
	if (!opts.screenshots.empty() && opts.screenshots.back() >= opts.frames)
	{
		core::throw_runtime_error(
			"--screenshot {} is past the last frame, {}",
			opts.screenshots.empty() ? 0 : opts.screenshots.back(),
			opts.frames - 1);
	}

	const auto dataRoot = std::filesystem::path(opts.project);
	const auto store    = assetlib::AssetStore(dataRoot);

	auto graphics = headless::CreateHeadlessGraphics(dataRoot);
	auto target   = headless::CreateHeadlessTarget(
		graphics,
		opts.width,
		opts.height,
		opts.taa,
		opts.renderScale);
	target->SetBloomEnabled(opts.bloom);
	target->SetPostProcessType(
		opts.postProcessType == "toon" ? bgl::PostProcessType::kToon :
										 bgl::PostProcessType::kFilmic);

	auto scene     = headless::CreateHeadlessScene(graphics);
	auto view      = graphics->CreateSceneView(scene, std::max(128u, 64u * opts.crowd));
	auto assets    = game::AssetManager(scene, dataRoot);
	auto envAssets = game::AssetManager(
		scene,
		opts.envRoot.empty() ? dataRoot : std::filesystem::path(opts.envRoot));

	const bool envLit = headless::LightView(view, envAssets, opts.env);

	// Additive on the environment above, which already integrates whatever sun its source HDR held
	// -- so a model measured under both is measured under two suns. One sun on screen: the PBR
	// sun and the toon character model's are set alike. See docs/ai_viewer.md.
	if (opts.sunIntensity > 0.0f)
	{
		const auto sun = bgl::DirectionalLightDesc{
			.direction = headless::SunDirection(
				glm::radians(opts.sunAzimuth),
				glm::radians(opts.sunElevation)),
			.color     = glm::vec3(opts.sunColor[0], opts.sunColor[1], opts.sunColor[2]),
			.intensity = opts.sunIntensity,
		};
		view->SetPbrDirectionalLight(sun);
		view->SetToonDirectionalLight(sun);
	}

	const bool lit = envLit || opts.sunIntensity > 0.0f;

	if (opts.poseBudget || opts.posePixels || opts.lod)
	{
		auto selection = view->GetLodSelection();
		if (opts.poseBudget)
			selection.poseBudget = *opts.poseBudget;
		if (opts.posePixels)
			selection.posePixels = *opts.posePixels;
		if (opts.lod)
			selection.forceLevel = static_cast<bgl::LodLevel>(*opts.lod);
		view->SetLodSelection(selection);
	}

	const bgl::Camera camera = opts.grass.empty() ?
	                               PlaceImport(opts, store, dataRoot, assets, view) :
	                               PlaceGrassPatch(opts, store, *scene, assets, view);

	std::cout << std::format(
		"{} frames at {} fps, {}x{}, render scale {}, {}, TAA {}, bloom {}, {} post-process, {} "
		"warm-up frames held at t = 0\n\n",
		opts.frames,
		opts.fps,
		opts.width,
		opts.height,
		opts.renderScale,
		opts.sunIntensity > 0.0f ?
			std::format("{}, sun {:.2f}", envLit ? "lit" : "unlit by env", opts.sunIntensity) :
			std::string(lit ? "lit" : "unlit"),
		opts.taa ? "on" : "off",
		opts.bloom ? "on" : "off",
		opts.postProcessType,
		opts.warmup);

	const std::filesystem::path outDir = std::filesystem::absolute(opts.outDir);
	std::filesystem::create_directories(outDir);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(opts.width), static_cast<float>(opts.height));
	job.time     = 0.0f;

	for (uint32_t i = 0; i < opts.warmup; ++i)
	{
		graphics->DrawFrame(target, job);
		graphics->WaitIdle();
	}

	target->SetGpuTimingEnabled(true);

	game::PassHistory history(opts.frames);
	uint64_t          lastRow = 0;
	const auto        collect = [&] {
		const bgl::PassTimings timings = graphics->GetPassTimings(target);
		if (timings.passes.empty() || timings.frame == lastRow)
			return;

		lastRow = timings.frame;
		if (history.SampleCount() < opts.frames)
			history.Append(timings);
	};

	auto nextShot = opts.screenshots.begin();
	for (uint32_t frame = 0; frame < opts.frames; ++frame)
	{
		job.time = static_cast<float>(frame) / opts.fps;
		graphics->DrawFrame(target, job);
		graphics->WaitIdle();
		collect();

		if (nextShot != opts.screenshots.end() && *nextShot == frame)
		{
			const std::filesystem::path png = outDir / std::format("frame_{:04}.png", frame);
			graphics->ScreenshotPng(target, png.string());
			std::cout
				<< std::format("frame {:>4}  t = {:.3f} s  {}\n", frame, job.time, png.string());
			++nextShot;
		}
	}

	for (uint32_t i = 0; i < c_DrainFrames && history.SampleCount() < opts.frames; ++i)
	{
		graphics->DrawFrame(target, job);
		graphics->WaitIdle();
		collect();
	}

	if (history.SampleCount() == 0)
	{
		std::cerr << "\nNo pass timings resolved: this device cannot sample a timestamp at a pass "
					 "boundary.\n";
		return 0;
	}

	const std::filesystem::path csvPath = outDir / "gpu_timings.csv";
	std::ofstream               csv(csvPath, std::ios::binary | std::ios::trunc);
	if (!csv)
	{
		core::throw_runtime_error("Could not write {}", csvPath.string());
	}
	csv << game::PassHistoryCsv(history);
	csv.close();

	std::cout << std::format("\n{} frames timed\n\n", history.SampleCount());
	headless::PrintPassCosts(std::cout, headless::SummarisePasses(history));
	std::cout << std::format("\n{}\n", csvPath.string());

	return 0;
}
catch (const std::exception& e)
{
	std::cerr << e.what() << '\n';
	return 1;
}
