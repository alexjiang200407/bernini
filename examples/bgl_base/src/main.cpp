#define NOMINMAX

#include <CLI/CLI.hpp>
#include <DemoWindow.h>
#include <FlyCamera.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>
#include <bgl/bgl.h>
#include <format>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

int APIENTRY
wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	try
	{
		uint32_t    width         = 800;
		uint32_t    height        = 600;
		bool        skyBoxEnabled = true;
		bool        headless      = false;
		uint32_t    frames        = 16;
		std::string modelPath     = "assets/apples";

		{
			CLI::App app{ "Bernini bgl_base example" };
			app.set_help_flag("--help", "Print this help message and exit");
			app.add_option("-w,--width", width, "Window width in pixels")
				->check(CLI::PositiveNumber);
			app.add_option("-h,--height", height, "Window height in pixels")
				->check(CLI::PositiveNumber);
			app.add_option("--model", modelPath, "Directory holding <name>.bmesh + its materials");
			app.add_option("-s,--skybox", skyBoxEnabled, "Enable skybox rendering");
			app.add_flag(
				"--headless",
				headless,
				"Render without a window: draw --frames frames, screenshot, and exit");
			app.add_option("--frames", frames, "Frames to render in --headless mode")
				->check(CLI::PositiveNumber);

			CLI11_PARSE(app, __argc, __wargv);
		}

		// The window is only created for the interactive path; headless renders to an offscreen target
		// so it can run unattended (and be diffed frame-to-frame) without a window popping up.
		auto windowOpts         = demo::WindowOptions{};
		windowOpts.width        = static_cast<int>(width);
		windowOpts.height       = static_cast<int>(height);
		windowOpts.title        = "Bernini bgl_base";
		windowOpts.borderless   = true;
		windowOpts.captureMouse = true;

		std::optional<demo::DemoWindow> wnd;
		if (!headless)
			wnd.emplace(windowOpts);

		auto gfxOpts                     = bgl::GraphicsOptions{};
		gfxOpts.enableDebugLayer         = true;
		gfxOpts.enableGPUValidationLayer = false;
		gfxOpts.enablePixDebug           = true;
		gfxOpts.logLevel                 = bgl::GraphicsOptions::LogLevel::kTrace;

		auto graphics = bgl::CreateGraphics(gfxOpts);

		auto targetDesc     = bgl::RenderTargetDesc{};
		targetDesc.width    = static_cast<int>(width);
		targetDesc.height   = static_cast<int>(height);
		targetDesc.headless = headless;
		targetDesc.wnd      = headless ? nullptr : wnd->NativeHandle();
		auto target         = graphics->CreateRenderTarget(targetDesc);

		auto sceneDesc                    = bgl::SceneDesc();
		sceneDesc.maxIndices              = 200000;
		sceneDesc.maxVertexBufferByteSize = 4000000;
		sceneDesc.maxGeom                 = 100;
		sceneDesc.maxMeshlets             = 8000;
		sceneDesc.maxSubmeshes            = 100;
		sceneDesc.maxPbrMaterials         = 100;

		auto scene = graphics->CreateScene(std::move(sceneDesc));
		auto view  = graphics->CreateSceneView(scene, 100);
		auto pmrem = scene->AddTextureAsset(assetlib::loadDDS("assets/pmrem.dds"));

		view->SetEnvironmentMap(
			{ scene->AddTextureAsset(assetlib::loadDDS("assets/iem.dds")),
		      pmrem,
		      scene->AddTextureAsset(assetlib::loadDDS("assets/brdf_lut.dds")) });

		if (skyBoxEnabled)
		{
			view->SetSkyBox({ pmrem });
		}

		const auto modelDir = std::filesystem::path(modelPath);
		const auto meshName = modelDir.filename().string();
		const auto model    = assetlib::load(modelDir / (meshName + ".bmesh"));

		auto       textureCache = std::unordered_map<std::string, bgl::TextureAssetHandle>();
		const auto loadTexture  = [&](const std::string& rel) -> bgl::TextureAssetHandle {
			if (rel.empty())
				return {};
			if (const auto it = textureCache.find(rel); it != textureCache.end())
				return it->second;
			const auto handle = scene->AddTextureAsset(assetlib::loadDDS(modelDir / rel), rel);
			textureCache.emplace(rel, handle);
			return handle;
		};

		auto materials = std::vector<bgl::MaterialHandle>();
		materials.reserve(model.materials.size());
		for (const auto& materialFile : model.materials)
		{
			const auto bmat = assetlib::loadMaterial(modelDir / materialFile);

			auto desc             = bgl::PbrMaterialDesc{};
			desc.baseColorFactor  = bmat.baseColorFactor;
			desc.metallicFactor   = bmat.metallicFactor;
			desc.roughnessFactor  = 0.0f;
			desc.baseColorTexture = loadTexture(bmat.baseColorTexture);
			desc.normalTexture    = loadTexture(bmat.normalTexture);
			desc.ormTexture       = loadTexture(bmat.ormTexture);

			materials.push_back(scene->CreatePbrMaterial(desc));
		}

		auto geoms = std::vector<bgl::GeomHandle>();
		geoms.reserve(model.meshes.size());
		for (uint32_t m = 0; m < model.meshes.size(); ++m)
			geoms.push_back(scene->AddStaticMesh(model, m, materials));

		const auto worldMatrix = [&](uint32_t node) {
			auto m = glm::mat4(1.0f);
			for (uint32_t n = node; n != assetlib::c_InvalidIndex; n = model.nodes[n].parent)
				m = assetlib::toMatrix(model.nodes[n].localTransform) * m;
			return m;
		};

		auto boundsMin = glm::vec3((std::numeric_limits<float>::max)());
		auto boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
		for (uint32_t n = 0; n < model.nodes.size(); ++n)
		{
			const auto& node = model.nodes[n];
			if (node.mesh == assetlib::c_InvalidIndex)
				continue;

			const auto world = worldMatrix(n);
			view->CreateStaticMeshInstance(geoms[node.mesh], world);

			const auto& meshEntry = model.meshes[node.mesh];
			for (uint32_t s = 0; s < meshEntry.submeshCount; ++s)
			{
				const auto& sm = model.submeshes[meshEntry.firstSubmesh + s];
				for (int corner = 0; corner < 8; ++corner)
				{
					const auto local = glm::vec3(
						(corner & 1) ? sm.aabbMax.x : sm.aabbMin.x,
						(corner & 2) ? sm.aabbMax.y : sm.aabbMin.y,
						(corner & 4) ? sm.aabbMax.z : sm.aabbMin.z);
					const auto worldPos = glm::vec3(world * glm::vec4(local, 1.0f));
					boundsMin           = glm::min(boundsMin, worldPos);
					boundsMax           = glm::max(boundsMax, worldPos);
				}
			}
		}

		const auto  center = (boundsMin + boundsMax) * 0.5f;
		const float radius = glm::max(glm::length(boundsMax - center), 0.001f);

		const float aspect = static_cast<float>(width) / static_cast<float>(height);
		const float fovY   = glm::radians(60.0f);

		// Pull the camera back to fit the model's bounding sphere, from a 3/4 angle.
		const auto  viewDir = glm::normalize(glm::vec3(0.4f, 0.35f, 1.0f));
		const float dist    = radius / glm::tan(fovY * 0.5f) * 1.4f;
		const auto  eye     = center + viewDir * dist;

		auto camera = bgl::Camera();
		camera.LookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(fovY, aspect, glm::max(radius * 0.02f, 0.01f), dist + radius * 4.0f);

		auto context     = bgl::RenderContext{};
		context.view     = view;
		context.camera   = camera;
		context.viewport = bgl::Viewport(static_cast<float>(width), static_cast<float>(height));

		if (headless)
		{
			for (uint32_t i = 0; i < frames; ++i)
			{
				graphics->DrawFrame(target, context);

				if (i == 0)
				{
					graphics->ScreenshotPng(target, "bgl_base.png");
				}
			}
		}
		else
		{
			auto     clock           = demo::DeltaClock{};
			uint32_t screenshotIndex = 0;
			while (!wnd->ShouldClose())
			{
				demo::PumpEvents();

				if (demo::ApplyFlyCam(camera, clock.Tick()))
				{
					context.camera = camera;
				}

				graphics->DrawFrame(target, context);

				if (demo::KeyPressed(demo::kScancodeF10))
				{
					graphics->ScreenshotPng(
						target,
						std::format("screenshot_{}.png", screenshotIndex++));
				}
			}
		}
	}
	catch (const std::runtime_error& e)
	{
		MessageBoxA(nullptr, e.what(), "Unhandled Error", MB_OK | MB_ICONERROR);
	}

	return 0;
}
