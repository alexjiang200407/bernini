#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <SDL3/SDL_messagebox.h>
#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/import_document.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>  // IWYU pragma: keep
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/math.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <gamelib/AssetManager.h>
#include <gamelib/ClipInfo.h>
#include <headless/framing.h>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	constexpr float c_CasterRadius = 0.35f;
	constexpr float c_Hover        = 1.75f;
	constexpr float c_Bob          = 0.2f;
	constexpr float c_DiscRadius   = 0.9f;
	constexpr float c_Intensity    = 0.85f;

	glm::mat4
	Box(const glm::vec3 centre, const glm::vec3 halfExtents)
	{
		return glm::scale(glm::translate(glm::mat4(1.0f), centre), halfExtents);
	}

	/** One node of the caster placed in the scene, so a slide can move all of them together. */
	struct CasterPart
	{
		bgl::MeshInstanceHandle instance;
		glm::mat4               local;
	};

	/** @throws std::runtime_error listing every clip when none is named `name`. */
	[[nodiscard]] uint32_t
	FindClip(const std::vector<game::ClipInfo>& clips, const std::string_view name)
	{
		if (name.empty())
			return 0;

		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			if (clips[i].name == name)
				return static_cast<uint32_t>(i);
		}

		std::string names;
		for (const game::ClipInfo& clip : clips)
		{
			names += std::format("\n  {}", clip.name);
		}
		core::throw_runtime_error("--clip {} is not in the clip set, which holds:{}", name, names);
	}
}

int
main(int argc, char** argv)
{
	// Outside the try so the handler knows whether anyone is looking at a window rather than at
	// stderr.
	bool headless = false;

	try
	{
		uint32_t    width        = 1280;
		uint32_t    height       = 720;
		float       travel       = 4.0f;
		float       period       = 8.0f;
		float       fadeHeight   = 2.5f;
		bool        taaEnabled   = true;
		uint32_t    frames       = 120;
		std::string dataRootPath = "assets/Data";
		std::string project;
		std::string importKey;
		std::string clipName;

		{
			auto app = CLI::App{ "A hovering caster whose blob shadow drapes over static crates" };
			app.add_option("--width", width, "Window width")->check(CLI::PositiveNumber);
			app.add_option("--height", height, "Window height")->check(CLI::PositiveNumber);
			app.add_option("--travel", travel, "How far the caster slides, in world units");
			app.add_option("--period", period, "Seconds for one round trip");
			app.add_option("--fade-height", fadeHeight, "Gap at which the shadow has fully faded")
				->check(CLI::PositiveNumber);
			app.add_option(
				"--project",
				project,
				"A project's Data directory; --import is keyed against it");
			app.add_option(
				"--import",
				importKey,
				"A .bimport in --project whose mesh replaces the hovering ball as the caster; a "
				"skinned one plays --clip while it slides");
			app.add_option(
				"--clip",
				clipName,
				"The clip a skinned caster plays; defaults to the first");
			app.add_flag("!--no-taa", taaEnabled, "Draw without temporal antialiasing");
			app.add_flag("--headless", headless, "Render offscreen for --frames and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);
			app.add_option("--data-root", dataRootPath, "Data root for the environment map");
			CLI11_PARSE(app, argc, argv);
		}

		core::throw_runtime_error_if(
			!importKey.empty() && project.empty(),
			"--import needs --project to key it against");

		// Headless renders offscreen, so the example is runnable unattended -- which is how anything
		// but a person can tell it still starts.
		std::optional<demo::DemoWindow> wnd;
		if (!headless)
		{
			wnd.emplace(
				demo::WindowOptions{ .width  = static_cast<int>(width),
			                         .height = static_cast<int>(height),
			                         .title  = "bernini - blob shadow decal" });
		}

		auto gfxOpts             = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer = true;

		// A project's materials may shade through its own surfaces, and those are registered only
		// at creation.
		if (!project.empty())
		{
			const auto surfaceDir = std::filesystem::path(project) / "Authored" / "Shaders";
			if (std::filesystem::is_directory(surfaceDir))
				gfxOpts.surfaceShaderDir = surfaceDir;
		}

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc       = bgl::RenderTargetDesc{};
		targetDesc.width      = static_cast<int>(width);
		targetDesc.height     = static_cast<int>(height);
		targetDesc.headless   = headless;
		targetDesc.wnd        = headless ? nullptr : wnd->NativeHandle();
		targetDesc.taaEnabled = taaEnabled;

		auto target = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 64;
		sceneDesc.initialSubmeshes            = 256;
		sceneDesc.initialMeshlets             = 8192;
		sceneDesc.initialVertexBufferByteSize = 8 * 1024 * 1024;
		sceneDesc.initialIndices              = 1024 * 1024;
		sceneDesc.initialPbrMaterials         = 64;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 64);

		auto assets = game::AssetManager(scene, dataRootPath);

		const auto env = assets.AcquireEnvironment("Authored/Environments/forest.benv");
		if (env.HasLighting())
			view->SetEnvironmentMap({ env.irradiance, env.prefilter });
		view->SetExposure(env.exposure);

		if (env.HasSky())
		{
			view->SetSkyBox({ env.skybox, env.skyMipLevel, 1.0f, env.skyRotationY });
		}

		// Matte and mid-grey, so the shadow reads as darkening rather than as a material change.
		const auto groundMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.9f });
		const auto crateMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.55f, 0.38f, 0.22f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.8f });
		const auto casterMaterial = scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc{ .baseColorFactor = glm::vec4(0.85f, 0.85f, 0.88f, 1.0f),
		                          .metallicFactor  = 0.0f,
		                          .roughnessFactor = 0.45f });

		const bgl::GeomHandle ground = scene->AddPlaneGeom(1, 1, 30.0f, 30.0f, groundMaterial);
		const bgl::GeomHandle crate  = scene->AddCubeGeom(crateMaterial);
		const bgl::GeomHandle ball   = scene->AddSphereGeom(32, 24, c_CasterRadius, casterMaterial);

		// Plane geoms are authored in XY; this lays the ground flat with its normal up.
		const glm::mat4 flat =
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		(void)view->CreateStaticMeshInstance(ground, flat);

		// Two receivers of different heights across the caster's path: the disc should climb onto
		// each top and drape over its edges rather than vanish beneath it. The cube geom spans
		// [-1, 1] on every axis, so the half extents are the box's half size.
		(void)view->CreateStaticMeshInstance(
			crate,
			Box(glm::vec3(2.5f, 0.6f, 0.0f), glm::vec3(1.5f, 0.6f, 1.5f)));
		(void)view->CreateStaticMeshInstance(
			crate,
			Box(glm::vec3(-2.5f, 0.3f, 0.0f), glm::vec3(1.0f, 0.3f, 1.0f)));

		// The caster: a skinned mesh from --project when one is named, else a hovering ball. A
		// skinned caster is what units are, so it never enters the static receiver and cannot
		// catch its own shadow; the ball is a static mesh and darkens its own underside -- the
		// artifact docs/passes.md describes.
		std::vector<CasterPart>           casterParts;
		std::optional<game::AssetManager> casterAssets;
		float                             hover      = c_Hover;
		float                             discRadius = c_DiscRadius;

		if (!importKey.empty())
		{
			const auto dataRoot = std::filesystem::path(project);
			const auto store    = assetlib::AssetStore(dataRoot);

			core::throw_runtime_error_if(
				!store.Exists(importKey),
				"{} is not in {}",
				importKey,
				std::filesystem::absolute(dataRoot).string());

			const assetlib::ImportDocument document =
				assetlib::loadImportDocument(store.GetFiles(), importKey);

			const std::string meshKey = document.GetMeshOutput();
			core::throw_runtime_error_if(meshKey.empty(), "{} produced no .bmesh", importKey);

			std::string animationsKey;
			for (const std::string& output : document.outputs)
			{
				if (assetlib::assetTypeFromExtension(output) == assetlib::AssetType::kAnimation)
					animationsKey = output;
			}

			for (const std::string_view key :
			     { std::string_view(meshKey), std::string_view(animationsKey) })
			{
				core::throw_runtime_error_if(
					!key.empty() && !store.Exists(key),
					"{} is not on disk. It is a derived container, which a project does not "
					"commit: `assetlib_cli migrate` writes back every one its .bimport documents "
					"name.",
					key);
			}

			const auto model = store.Load<assetlib::BMesh>(meshKey);

			std::vector<std::optional<assetlib::Bounds>> posedBounds;
			std::optional<assetlib::AnimationSet>        animations;
			std::optional<assetlib::Skeleton>            skeleton;
			const bool                                   rigged = !animationsKey.empty();
			if (rigged)
			{
				animations  = store.Load<assetlib::AnimationSet>(animationsKey);
				skeleton    = store.Load<assetlib::Skeleton>(animations->skeleton);
				posedBounds = assetlib::findPosedBounds(*animations, model, *skeleton);
			}

			casterAssets.emplace(scene, dataRoot);

			auto bounds = headless::EmptyBounds();

			struct SkinnedPlacement
			{
				bgl::GeomHandle geom;
				glm::mat4       local;
			};
			std::vector<SkinnedPlacement> skinnedPlacements;
			std::vector<game::ClipInfo>   clips;

			for (uint32_t n = 0; n < model.nodes.size(); ++n)
			{
				const uint32_t meshIndex = model.nodes[n].mesh;
				if (meshIndex == assetlib::c_InvalidIndex)
					continue;

				const glm::mat4 local = headless::InstanceTransform(model, n);
				if (!rigged || !assetlib::isSkinned(model, meshIndex))
				{
					casterParts.emplace_back(
						casterAssets->CreateInstance(
							view,
							casterAssets->AcquireMesh(meshKey, meshIndex),
							local),
						local);
					headless::GrowBounds(
						bounds,
						local,
						headless::MeshEntryBounds(model, meshIndex));
					continue;
				}

				const assetlib::Bounds posed =
					posedBounds[meshIndex] ?
						*posedBounds[meshIndex] :
						assetlib::posedBounds(model, meshIndex, *skeleton, *animations);

				game::AssetManager::SkinnedMesh acquired =
					casterAssets->AcquireSkinnedMesh(meshKey, animationsKey, {}, meshIndex, posed);
				skinnedPlacements.emplace_back(acquired.geom, local);
				clips = std::move(acquired.clips);
				headless::GrowBounds(bounds, local, posed);
			}

			const uint32_t clip = skinnedPlacements.empty() ? 0 : FindClip(clips, clipName);
			for (const SkinnedPlacement& placement : skinnedPlacements)
			{
				casterParts.emplace_back(
					casterAssets->CreateSkinnedInstance(
						view,
						placement.geom,
						placement.local,
						bgl::SkinnedInstanceDesc{ clip,
				                                  0.0f,
				                                  1.0f,
				                                  bgl::PoseSource::kPerInstance }),
					placement.local);
			}
			core::throw_runtime_error_if(casterParts.empty(), "{} placed no meshes", importKey);

			// A rigged placement's origin is at its feet, so it hovers by the gap under them; size
			// the disc to its posed footprint.
			const glm::vec3 size = bounds.max - bounds.min;
			discRadius           = std::max(0.5f * std::max(size.x, size.z), 0.4f);
			hover                = 1.4f;
		}
		else
		{
			casterParts.emplace_back(
				view->CreateStaticMeshInstance(
					ball,
					glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, c_Hover, 0.0f))),
				glm::mat4(1.0f));
		}

		const auto shadow = bgl::BlobShadowDesc{ .radius     = discRadius,
			                                     .intensity  = c_Intensity,
			                                     .fadeHeight = fadeHeight };
		view->SetBlobShadow(casterParts.front().instance, shadow);

		// A grounded twin for contrast: its disc is at full strength and never moves.
		const bgl::MeshInstanceHandle rester = view->CreateStaticMeshInstance(
			ball,
			glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, c_CasterRadius, 2.5f)));
		view->SetBlobShadow(rester, shadow);

		const float aspect = static_cast<float>(width) / static_cast<float>(height);

		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 5.0f, 11.0f),
				glm::vec3(0.0f, 0.8f, 0.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(glm::radians(60.0f), aspect, 0.5f, 500.0f);

		auto job     = bgl::RenderJob{};
		job.view     = view;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		const auto slide = [&](float seconds) {
			const float phase =
				period > 0.0f ? seconds * 2.0f * static_cast<float>(core::c_Pi) / period : 0.0f;
			const glm::mat4 placement = glm::translate(
				glm::mat4(1.0f),
				glm::vec3(travel * std::sin(phase), hover + c_Bob * std::sin(2.0f * phase), 0.0f));
			for (const CasterPart& part : casterParts)
			{
				view->SetInstanceTransform(part.instance, placement * part.local);
			}
		};

		if (headless)
		{
			// A fixed step, so the run is the same every time. The default frame count parks the
			// caster on the tall crate's edge, the disc half on its top and half on the ground.
			constexpr float c_Step = 1.0f / 60.0f;

			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				const float seconds = static_cast<float>(frame) * c_Step;
				slide(seconds);
				job.time = seconds;
				graphics->DrawFrame(target, job);
			}

			graphics->ScreenshotPng(target, "bgl_blob_shadow.png");
			return 0;
		}

		std::cout
			<< "WASD to fly, hold Shift and move the mouse to look. The hovering caster slides "
			   "over "
			   "two crates: its shadow climbs onto each top, drapes over the edges, and fades "
			   "with the gap. The resting ball's disc stays grounded.\n";

		auto  clock   = demo::DeltaClock{};
		float elapsed = 0.0f;

		while (!wnd->ShouldClose())
		{
			demo::PumpEvents();

			const float dt = clock.Tick();
			elapsed += dt;

			if (demo::ApplyFlyCam(camera, dt))
			{
				job.camera = camera;
			}

			slide(elapsed);

			job.time = elapsed;
			graphics->DrawFrame(target, job);
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "bgl_blob_shadow: " << e.what() << '\n';

		if (!headless)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bgl_blob_shadow", e.what(), nullptr);
		}

		return 1;
	}
}
