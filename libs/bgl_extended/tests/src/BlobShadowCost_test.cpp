#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/PassTiming.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <vector>

// What blob-shadow discs cost the forward pass by where their volumes lie against the camera, at
// 3840x2160: a runner's view down its track, with trees' discs on the section behind the camera
// and on the one ahead. Not a test of behaviour: it is run by hand -- `just run bgl_extended_tests
// -- "[.blobshadowcost]"` -- and the numbers are read off the warnings it prints.
namespace
{
	constexpr uint32_t c_Width  = 3840;
	constexpr uint32_t c_Height = 2160;

	constexpr uint32_t c_DiscsPerSide = 48;

	// The median Forward time over a handful of frames after a warm-up.
	double
	ForwardMs(bgl::IGraphics& gfx, const bgl::RenderTargetRef& target, const bgl::RenderJob& job)
	{
		target->SetGpuTimingEnabled(true);
		std::vector<double> samples;
		for (int frame = 0; frame < 12; ++frame)
		{
			gfx.DrawFrame(target, job);
			gfx.WaitIdle();
			if (frame < 4)
				continue;
			const bgl::PassTimings timings = gfx.GetPassTimings(target);
			for (const bgl::PassTiming& row : timings.passes)
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

TEST_CASE("what blob-shadow discs behind and ahead of the camera cost Forward", "[.blobshadowcost]")
{
	auto opts           = bgl::GraphicsOptions();
	opts.shaderCacheDir = bgl::test::ShaderCacheDir();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc       = bgl::RenderTargetDesc();
	targetDesc.width      = static_cast<int>(c_Width);
	targetDesc.height     = static_cast<int>(c_Height);
	targetDesc.headless   = true;
	targetDesc.taaEnabled = true;
	auto target           = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 128;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 100000;
	sceneDesc.initialIndices              = 4000;
	sceneDesc.initialPbrMaterials         = 8;
	auto scene                            = gfx->CreateScene(sceneDesc);
	auto view                             = gfx->CreateSceneView(scene, 256);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	scene->SetGround(bgl::GroundPlaneDesc());

	auto whiteDesc            = bgl::PbrMaterialDesc();
	whiteDesc.baseColorFactor = glm::vec4(1.0f);
	whiteDesc.metallicFactor  = 0.0f;
	whiteDesc.roughnessFactor = 1.0f;
	const auto white          = scene->CreatePbrMaterial(whiteDesc);

	const glm::mat4 flat =
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));

	const auto groundGeom = scene->AddPlaneGeom(1, 1, 400.0f, 400.0f, white);
	const auto casterGeom = scene->AddPlaneGeom(1, 1, 0.5f, 0.5f, white);
	view->CreateStaticMeshInstance(groundGeom, flat);

	// Two rows either side of the track, one disc every 2 m, starting 3 m from the eye.
	const auto row = [&](const float direction) {
		auto casters = std::vector<bgl::MeshInstanceHandle>();
		for (uint32_t i = 0; i < c_DiscsPerSide; ++i)
		{
			const float x = (i % 2 == 0) ? -3.0f : 3.0f;
			const float z = direction * (3.0f + 2.0f * static_cast<float>(i / 2));
			casters.emplace_back(view->CreateStaticMeshInstance(
				casterGeom,
				glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z)) * flat));
		}
		return casters;
	};

	const std::vector<bgl::MeshInstanceHandle> behind = row(1.0f);
	const std::vector<bgl::MeshInstanceHandle> ahead  = row(-1.0f);

	auto disc       = bgl::BlobShadowDesc();
	disc.radius     = 2.6f;
	disc.intensity  = 0.6f;
	disc.fadeHeight = 2.0f;
	disc.casterLift = 1.0f;

	const auto setDiscs = [&](const std::vector<bgl::MeshInstanceHandle>& casters, bool on) {
		for (const bgl::MeshInstanceHandle caster : casters)
		{
			if (on)
				view->SetBlobShadow(caster, disc);
			else
				view->ClearBlobShadow(caster);
		}
	};

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 2.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, -10.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.1f,
			500.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	WARN("no discs: Forward " << ForwardMs(*gfx, target, job) << " ms");

	setDiscs(ahead, true);
	WARN(c_DiscsPerSide << " discs ahead: Forward " << ForwardMs(*gfx, target, job) << " ms");

	setDiscs(behind, true);
	WARN(
		c_DiscsPerSide << " ahead + " << c_DiscsPerSide << " behind: Forward "
					   << ForwardMs(*gfx, target, job) << " ms");

	setDiscs(ahead, false);
	WARN(c_DiscsPerSide << " discs behind: Forward " << ForwardMs(*gfx, target, job) << " ms");
}
