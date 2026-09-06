#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>
#include <bgl/types/EnvironmentMapDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <core/platform/util.h>
#include <cstdint>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What the cha800 face close-up costs Forward, part by part, on the grid the editor drew it on at
// 2x. Not a test of behaviour and not runnable in CI: it wants the test project's cooked cha800
// and the repo's forest environment, reached through BERNINI_CHECKOUT, and it is run by hand --
// `BERNINI_CHECKOUT=$PWD just run gamelib_tests -- "[.cha800cost]"` -- with the numbers read off
// the warnings it prints. Every mesh entry of the file is instanced under its node's transform,
// as the editor places it, and the camera fills the frame with the face from the front.
namespace
{
	constexpr uint32_t c_Width  = 2292;
	constexpr uint32_t c_Height = 1996;

	constexpr std::string_view c_Mesh = "Derived/Meshes/AdaWong/cha800_00.reduced.bmesh";
	constexpr std::string_view c_Env  = "Authored/Environments/forest.benv";

	glm::mat4
	WorldTransform(const assetlib::BMesh& mesh, uint32_t nodeIndex)
	{
		auto     world = glm::mat4(1.0f);
		uint32_t index = nodeIndex;
		while (index != assetlib::c_InvalidIndex && index < mesh.nodes.size())
		{
			const assetlib::Node& node = mesh.nodes[index];
			world                      = assetlib::toMatrix(node.localTransform) * world;
			index                      = node.parent;
		}
		return world;
	}

	struct Part
	{
		std::string     name;
		uint32_t        meshIndex = 0;
		glm::mat4       transform{ 1.0f };
		bgl::GeomHandle geom;
		glm::vec3       boxMin{ 0.0f };
		glm::vec3       boxMax{ 0.0f };
	};

	double
	ForwardMs(bgl::IGraphics& gfx, const bgl::RenderTargetRef& target, const bgl::RenderJob& job)
	{
		std::vector<double> samples;
		for (int frame = 0; frame < 8; ++frame)
		{
			gfx.DrawFrame(target, job);
			gfx.WaitIdle();
			if (frame < 3)
				continue;
			for (const bgl::PassTiming& row : gfx.GetPassTimings(target))
			{
				if (row.name == "Forward 0")
					samples.push_back(row.milliseconds);
			}
		}
		REQUIRE(!samples.empty());
		std::ranges::sort(samples);
		return samples[samples.size() / 2];
	}
}

TEST_CASE("what the cha800 face close-up costs Forward, part by part", "[.cha800cost]")
{
	const std::optional<std::string> checkout = core::env_var("BERNINI_CHECKOUT");
	if (!checkout.has_value())
	{
		SKIP("BERNINI_CHECKOUT is not set");
	}

	const auto projectRoot = std::filesystem::path(*checkout) / "test-project" / "Data";
	const auto assetsRoot  = std::filesystem::path(*checkout) / "assets" / "Data";

	auto opts             = bgl::GraphicsOptions();
	opts.enableDebugLayer = false;
	opts.shaderCacheDir   = "shadercache";
	opts.maxTextures      = 512;
	opts.maxSrvs          = 1024;
	opts.maxCbvSrvUavs    = 4096;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;
	auto target           = gfx->CreateRenderTarget(targetDesc);
	target->SetGpuTimingEnabled(true);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 64;
	sceneDesc.initialMeshlets             = 65536;
	sceneDesc.initialSubmeshes            = 256;
	sceneDesc.initialVertexBufferByteSize = 64u << 20;
	sceneDesc.initialIndices              = 4000000;
	sceneDesc.initialPbrMaterials         = 64;
	auto scene                            = gfx->CreateScene(sceneDesc);

	auto       assets    = game::AssetManager(scene, projectRoot);
	auto       envAssets = game::AssetManager(scene, assetsRoot);
	const auto env       = envAssets.AcquireEnvironment(c_Env);
	REQUIRE(env.HasLighting());

	const auto file = assetlib::AssetStore(projectRoot).Load<assetlib::BMesh>(c_Mesh);

	std::vector<Part> parts;
	for (uint32_t n = 0; n < file.nodes.size(); ++n)
	{
		const assetlib::Node& node = file.nodes[n];
		if (node.mesh == assetlib::c_InvalidIndex)
			continue;

		Part part;
		part.meshIndex = node.mesh;
		part.name      = std::string(file.stringPool.at(file.meshes[node.mesh].nameOffset));
		part.transform =
			assetlib::isSkinned(file, node.mesh) ? glm::mat4(1.0f) : WorldTransform(file, n);
		part.geom = assets.AcquireMesh(c_Mesh, node.mesh);

		const assetlib::Mesh& entry = file.meshes[node.mesh];
		part.boxMin                 = glm::vec3(1e30f);
		part.boxMax                 = glm::vec3(-1e30f);
		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			const assetlib::Submesh& sub = file.submeshes[entry.firstSubmesh + s];
			part.boxMin                  = glm::min(part.boxMin, sub.aabbMin);
			part.boxMax                  = glm::max(part.boxMax, sub.aabbMax);
		}
		parts.push_back(part);
	}
	REQUIRE(!parts.empty());

	for (const Part& p : parts)
	{
		const assetlib::Mesh& entry     = file.meshes[p.meshIndex];
		uint32_t              meshlets  = 0;
		uint32_t              triangles = 0;
		for (uint32_t s = 0; s < entry.submeshCount; ++s)
		{
			meshlets += file.submeshes[entry.firstSubmesh + s].meshletCount;
			triangles += file.submeshes[entry.firstSubmesh + s].indexCount / 3;
		}
		WARN("part " << p.name << ": " << meshlets << " meshlets, " << triangles << " triangles");
	}

	// The face's own box, in world: what the camera frames.
	const auto face = std::ranges::find_if(parts, [](const Part& p) {
		return p.name.find("Face") != std::string::npos;
	});
	REQUIRE(face != parts.end());

	const glm::vec3 faceMin = glm::vec3(face->transform * glm::vec4(face->boxMin, 1.0f));
	const glm::vec3 faceMax = glm::vec3(face->transform * glm::vec4(face->boxMax, 1.0f));
	const glm::vec3 centre  = 0.5f * (faceMin + faceMax);
	const float     height  = std::abs(faceMax.y - faceMin.y);

	// Distance at which the face's height fills about 90% of the frame's, through 60 degrees.
	const float fov = glm::radians(60.0f);
	// BERNINI_COST_DISTANCE scales the framing: above 1 the face shrinks in the frame, which is
	// what separates a cost in fragments from one in geometry.
	const std::optional<std::string> distanceScale = core::env_var("BERNINI_COST_DISTANCE");
	const float distance  = (height / 0.9f) * 0.5f / std::tan(fov * 0.5f) *
	                        (distanceScale.has_value() ? std::stof(*distanceScale) : 1.0f);
	const float aspect    = static_cast<float>(c_Width) / static_cast<float>(c_Height);
	const float nearPlane = std::max(0.01f, distance * 0.05f);

	const auto viewFor = [&](std::string_view label, auto keep) {
		auto view = gfx->CreateSceneView(scene, 64);
		view->SetEnvironmentMap(bgl::EnvironmentMapDesc(env.irradiance, env.prefilter));
		view->SetExposure(env.exposure);
		if (env.HasSky())
			view->SetSkyBox(bgl::SkyboxDesc{ env.skybox });

		for (const Part& p : parts)
		{
			if (keep(p))
				(void)view->CreateStaticMeshInstance(p.geom, p.transform);
		}

		for (const float side : { 1.0f, -1.0f })
		{
			auto camera = bgl::Camera();
			camera
				.LookAt(
					centre + glm::vec3(0.0f, 0.0f, side * distance),
					centre,
					glm::vec3(0.0f, 1.0f, 0.0f))
				.Perspective(fov, aspect, nearPlane, distance * 20.0f);

			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = camera;
			job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

			WARN(
				label << (side > 0.0f ? " from +Z" : " from -Z") << ": Forward "
					  << ForwardMs(*gfx, target, job) << " ms");
		}
	};

	const auto has = [](const Part& p, std::string_view needle) {
		return p.name.find(needle) != std::string::npos;
	};

	viewFor("everything", [](const Part&) { return true; });
	viewFor("no hair", [&](const Part& p) { return !has(p, "Hair"); });
	viewFor("hair only", [&](const Part& p) { return has(p, "Hair"); });
	viewFor("no eyes/lash", [&](const Part& p) {
		return !has(p, "Eye") && !has(p, "Lash") && !has(p, "Blow");
	});
	viewFor("face+mouth only", [&](const Part& p) { return has(p, "Face") || has(p, "Mouth"); });
	viewFor("head only (cha80010+cha80020)", [&](const Part& p) {
		return has(p, "cha80010") || has(p, "cha80020");
	});

	gfx->WaitIdle();
}
