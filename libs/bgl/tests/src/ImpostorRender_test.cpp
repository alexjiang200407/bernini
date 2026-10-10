#include "util/GoldenImage.h"
#include "util/ImpostorMesh.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/glm.h>
#include <bgl/types/Camera.h>
#include <bgl/types/DirectionalLightDesc.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// The impostor stage: a placement past its geom's last level drawn as its impostor, and one whose
// geom has none drawn as nothing, as before impostors existed. The mesh is a 2 x 2 quad drawn unlit
// white; the impostor is not baked from it but written by hand -- every frame a red disc half the
// frame across, facing the viewer -- so what is drawn tells the two apart, and the frame read is
// the one the cull and the stage chose rather than the bake's.

namespace
{
	constexpr int c_Size = 64;

	// The placement stands 2 in front of a 90-degree camera, so its unit sphere spans x and y in
	// [16, 48], and the disc [24, 40]. The boxes sit inside the disc, and outside it inside the quad.
	constexpr int c_DiscX = 28, c_DiscY = 28, c_DiscW = 8;
	constexpr int c_RimX = 18, c_RimY = 18, c_RimW = 4;

	struct Probe
	{
		bgl::GraphicsRef     gfx;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		bgl::GeomHandle      geom;
		float                time = 0.0f;

		Probe(const bool impostor, const float lastFloor)
		{
			auto opts                      = bgl::test::GraphicsSetup();
			opts.gpuContext.shaderCacheDir = bgl::test::ShaderCacheDir();
			gfx                            = bgl::test::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			scene = gfx->CreateScene(bgl::SceneDesc());
			view  = gfx->CreateSceneView(scene, 4);
			bgl::test::ApplyEnvironment(scene.Get(), view.Get());
			view->SetDirectionalLight(
				{ .direction = glm::vec3(0.0f, 0.0f, -1.0f),
			      .color     = glm::vec3(1.0f),
			      .intensity = 0.5f });

			const auto mesh = bgl::test::MakeDiscImpostorMesh(impostor, lastFloor);
			geom            = scene->AddStaticMeshGeom(bgl::StaticMeshGeomDesc().SetMesh(&mesh));
			REQUIRE(geom.IsValid());
			view->CreateStaticMeshInstance(
				bgl::StaticMeshInstanceDesc().SetGeom(geom).SetTransform(
					glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f))));

			auto targetDesc       = bgl::RenderTargetDesc();
			targetDesc.width      = c_Size;
			targetDesc.height     = c_Size;
			targetDesc.headless   = true;
			targetDesc.taaEnabled = false;
			target                = gfx->CreateRenderTarget(targetDesc);
		}

		struct Shot
		{
			glm::vec3 disc;
			glm::vec3 rim;
			glm::vec3 background;  // a corner nothing stands in front of

			[[nodiscard]] bool
			DiscIsBackground() const
			{
				return glm::all(glm::lessThan(glm::abs(disc - background), glm::vec3(0.02f)));
			}

			[[nodiscard]] bool
			RimIsBackground() const
			{
				return glm::all(glm::lessThan(glm::abs(rim - background), glm::vec3(0.02f)));
			}
		};

		Shot
		Frame(const std::string& name)
		{
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.viewport = bgl::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size));
			job.camera   = bgl::Camera()
			                   .LookAt(
								   glm::vec3(0.0f),
								   glm::vec3(0.0f, 0.0f, -1.0f),
								   glm::vec3(0.0f, 1.0f, 0.0f))
			                   .Perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
			job.time     = time;
			gfx->DrawFrame(target, job);
			time += 0.03f;

			const std::string path =
				(std::filesystem::temp_directory_path() / ("bernini_impostor_" + name + ".png"))
					.string();
			gfx->ScreenshotPng(target, path);
			const auto disc = bgl::test::MeanColor(path, c_DiscX, c_DiscY, c_DiscW, c_DiscW);
			const auto rim  = bgl::test::MeanColor(path, c_RimX, c_RimY, c_RimW, c_RimW);
			const auto back = bgl::test::MeanColor(path, 1, 1, 4, 4);
			return { glm::vec3(disc.r, disc.g, disc.b),
				     glm::vec3(rim.r, rim.g, rim.b),
				     glm::vec3(back.r, back.g, back.b) };
		}

		void
		Force(const bool impostor)
		{
			auto desc        = bgl::LodSelectionDesc();
			desc.fadeSeconds = 0.0f;
			if (impostor)
				desc.forceImpostor = true;
			else
				desc.forceLevel = bgl::LodLevel::kLod0;
			view->SetLodSelection(desc);
		}
	};
}

TEST_CASE("a placement forced past its last level draws its impostor, lit", "[impostor][render]")
{
	auto probe = Probe(true, 0.0f);

	probe.Force(false);
	const auto mesh = probe.Frame("mesh");
	CHECK(mesh.disc.r > 0.5f);
	CHECK(mesh.disc.g > 0.5f);  // the unlit quad is white
	CHECK(mesh.rim.r > 0.5f);

	probe.Force(true);
	const auto impostor = probe.Frame("impostor");
	CHECK(impostor.disc.r > 0.1f);  // red, lit by the sky and the sun
	CHECK(impostor.disc.r > 2.0f * impostor.disc.g);
	CHECK(impostor.RimIsBackground());  // past the disc the atlas covers nothing
}

TEST_CASE(
	"a geom with no impostor draws nothing past its last level, as before",
	"[impostor][render]")
{
	auto probe = Probe(false, 0.0f);
	probe.Force(true);
	const auto shot = probe.Frame("none");
	CHECK(shot.DiscIsBackground());
	CHECK(shot.RimIsBackground());
}

TEST_CASE(
	"a placement smaller than its last level's floor draws its impostor by size",
	"[impostor][render]")
{
	// The placement's sphere spans about 45 pixels, under a last floor of 60.
	SECTION("with one")
	{
		auto       probe = Probe(true, 60.0f);
		const auto shot  = probe.Frame("by_size");
		CHECK(shot.disc.r > 0.1f);
		CHECK(shot.disc.r > 2.0f * shot.disc.g);
	}
	SECTION("without one")
	{
		auto       probe = Probe(false, 60.0f);
		const auto shot  = probe.Frame("by_size_none");
		CHECK(shot.DiscIsBackground());
	}
	SECTION("over the floor, the mesh")
	{
		auto       probe = Probe(true, 20.0f);
		const auto shot  = probe.Frame("by_size_mesh");
		CHECK(shot.disc.g > 0.5f);
	}
}

TEST_CASE("a geom deleted with its impostor gives the atlases back", "[impostor][render]")
{
	auto probe = Probe(true, 0.0f);
	probe.Force(true);
	(void)probe.Frame("before_delete");

	probe.view = probe.gfx->CreateSceneView(probe.scene, 4);
	probe.scene->DeleteGeom(probe.geom);
	const auto shot = probe.Frame("after_delete");
	CHECK(shot.DiscIsBackground());
}

TEST_CASE("a placement dissolves from its last level into its impostor", "[impostor][render]")
{
	// Over a last floor of 20 the quad draws; a threshold scale of 4 puts it under, and the change
	// dissolves over fadeSeconds -- 0.15 s, five of these 30 ms frames.
	auto probe = Probe(true, 20.0f);
	for (int frame = 0; frame < 3; ++frame) (void)probe.Frame("mesh_settled");
	const auto mesh = probe.Frame("mesh_at_rest");

	auto desc       = bgl::LodSelectionDesc();
	desc.pixelScale = 4.0f;
	probe.view->SetLodSelection(desc);
	const auto fading = probe.Frame("fading");
	for (int frame = 0; frame < 8; ++frame) (void)probe.Frame("impostor_settling");
	const auto impostor = probe.Frame("impostor_at_rest");

	// Mid-dissolve the disc holds both the white quad and the red impostor, dithered: its green lies
	// between the two at rest.
	INFO(
		"green: mesh " << mesh.disc.g << ", fading " << fading.disc.g << ", impostor "
					   << impostor.disc.g);
	CHECK(fading.disc.g < mesh.disc.g - 0.05f);
	CHECK(fading.disc.g > impostor.disc.g + 0.05f);
	CHECK(impostor.disc.r > 2.0f * impostor.disc.g);
}
