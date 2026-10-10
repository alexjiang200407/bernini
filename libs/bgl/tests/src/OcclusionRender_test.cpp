#include "util/GoldenImage.h"
#include "util/GpuValidation.h"
#include "util/ImpostorMesh.h"
#include "util/TestEnvironment.h"
#include "util/TestGraphics.h"
#include "util/TestOptions.h"
#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/types/Camera.h>
#include <bgl/types/GeomHandle.h>
#include <bgl/types/InstanceDesc.h>
#include <bgl/types/MaterialHandle.h>
#include <bgl/types/PassTiming.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgl/types/Viewport.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The occlusion cull changes what is drawn and never what is seen: a sequence rendered with it
// on is, frame for frame, the sequence rendered with it off. The sequence holds the cases that
// could differ -- a cube hidden behind a wall, one beside it in plain view, and a camera move
// that brings the hidden cube out from behind the wall, which has to show the frame it is
// revealed and not the one after.

namespace
{
	constexpr uint32_t c_W      = 192;
	constexpr uint32_t c_H      = 144;
	constexpr int      c_Frames = 8;

	// The camera slides right over the sequence, always looking at what stands behind the wall: at
	// first straight through the wall, then, once it has slid far enough, past the wall's edge.
	bgl::Camera
	CameraAt(const int frame)
	{
		const float x = frame < 4 ? 0.0f : 6.0f * static_cast<float>(frame - 3);
		return bgl::Camera()
		    .LookAt(
				glm::vec3(x, 1.5f, 8.0f),
				glm::vec3(0.0f, 1.0f, -6.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
		    .Perspective(glm::radians(60.0f), static_cast<float>(c_W) / c_H, 0.5f, 200.0f);
	}

	struct Sequence
	{
		std::vector<std::string> frames;

		// The names of the passes the last timed frame ran.
		std::vector<std::string> passes;
	};

	// What stands hidden behind the wall until the camera's move reveals it.
	enum class Hidden : uint8_t
	{
		kCube,
		kImpostor,  // a geom always drawn as its impostor: the disc, by size past its last level
		kNothing,
	};

	Sequence
	RenderSequence(const bool occlusion, const Hidden hidden = Hidden::kCube)
	{
		auto opts                                = bgl::test::GraphicsSetup();
		opts.gpuContext.shaderCacheDir           = bgl::test::ShaderCacheDir();
		opts.gpuContext.enableDebugLayer         = true;
		opts.gpuContext.enableGPUValidationLayer = bgl::test::GpuValidationEnabled();

		auto gfx = bgl::test::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto td       = bgl::RenderTargetDesc();
		td.width      = static_cast<int>(c_W);
		td.height     = static_cast<int>(c_H);
		td.headless   = true;
		td.taaEnabled = false;
		auto target   = gfx->CreateRenderTarget(td);
		target->SetGpuTimingEnabled(true);

		auto scene = gfx->CreateScene(bgl::SceneDesc());
		auto view  = gfx->CreateSceneView(scene, 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());
		view->SetOcclusionCulling(occlusion);

		auto desc                          = bgl::PbrMaterialDesc();
		desc.baseColorFactor               = glm::vec4(0.7f, 0.5f, 0.3f, 1.0f);
		desc.metallicFactor                = 0.0f;
		desc.roughnessFactor               = 0.6f;
		const bgl::MaterialHandle material = scene->CreatePbrMaterial(desc);

		const auto cube = scene->AddCubeGeom(material);
		REQUIRE(cube.IsValid());

		// A wall across the view, a cube hidden behind it, and one in plain view to its left.
		const auto place = [&](const glm::vec3& at, const glm::vec3& size) {
			REQUIRE(view->CreateStaticMeshInstance(
							bgl::StaticMeshInstanceDesc().SetGeom(cube).SetTransform(
								glm::scale(glm::translate(glm::mat4(1.0f), at), size)))
			            .IsValid());
		};
		place(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(4.0f, 2.0f, 0.25f));
		if (hidden == Hidden::kCube)
		{
			place(glm::vec3(0.0f, 1.0f, -6.0f), glm::vec3(1.0f));
		}
		place(glm::vec3(-7.0f, 1.0f, -6.0f), glm::vec3(1.0f));

		// A floor no frame could draw its mesh over: always past its last level, so always the
		// impostor.
		const auto impostorMesh = bgl::test::MakeDiscImpostorMesh(true, 1.0e6f);
		if (hidden == Hidden::kImpostor)
		{
			const auto impostor =
				scene->AddStaticMeshGeom(bgl::StaticMeshGeomDesc().SetMesh(&impostorMesh));
			REQUIRE(impostor.IsValid());
			REQUIRE(view->CreateStaticMeshInstance(
							bgl::StaticMeshInstanceDesc().SetGeom(impostor).SetTransform(
								glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -6.0f))))
			            .IsValid());
		}

		auto sequence = Sequence();
		for (int frame = 0; frame < c_Frames; ++frame)
		{
			auto job     = bgl::RenderJob();
			job.view     = view;
			job.camera   = CameraAt(frame);
			job.viewport = bgl::Viewport(static_cast<float>(c_W), static_cast<float>(c_H));
			gfx->DrawFrame(target, job);

			sequence.frames.push_back(
				std::format(
					"occlusion_{}_{}_{}.png",
					std::to_underlying(hidden),
					occlusion ? "on" : "off",
					frame));
			gfx->ScreenshotPng(target, sequence.frames.back());
		}
		for (const bgl::PassTiming& pass : gfx->GetPassTimings(target).passes)
		{
			sequence.passes.push_back(pass.name);
		}
		return sequence;
	}
}

TEST_CASE(
	"Occlusion culling draws the same frames as drawing everything, a reveal included",
	"[culling][occlusion][render]")
{
	const Sequence                  sequenceOn  = RenderSequence(true);
	const Sequence                  sequenceOff = RenderSequence(false);
	const std::vector<std::string>& on          = sequenceOn.frames;
	const std::vector<std::string>& off         = sequenceOff.frames;

	// The premise of the comparison: with it on the frame ran both phases and both builds, and
	// with it off neither.
	const auto ran = [](const Sequence& sequence, const std::string_view prefix) {
		return std::ranges::any_of(sequence.passes, [&](const std::string& name) {
			return name.starts_with(prefix);
		});
	};
	CHECK(ran(sequenceOn, "Cull Occluded"));
	CHECK(ran(sequenceOn, "Forward World Phase 2"));
	CHECK(ran(sequenceOn, "HZB Phase"));
	CHECK(ran(sequenceOn, "HZB Frame"));
	CHECK_FALSE(ran(sequenceOff, "Cull Occluded"));
	CHECK_FALSE(ran(sequenceOff, "HZB Frame"));

	// The premise: the wall is on screen, the cube hides behind it at first -- the frame is the
	// one without it -- and comes into view by the end.
	const Sequence without = RenderSequence(false, Hidden::kNothing);
	CHECK(bgl::test::MaxChannelDelta(off.front(), without.frames.front()) == 0.0f);
	CHECK(bgl::test::MaxChannelDelta(off.back(), without.frames.back()) > 0.0f);
	CHECK(
		bgl::test::MeanColor(
			off.front(),
			static_cast<int>(c_W / 2) - 8,
			static_cast<int>(c_H / 2) - 8,
			16,
			16)
			.Luma() > 0.02f);
	CHECK(bgl::test::MaxChannelDelta(off.front(), off.back()) > 0.0f);

	REQUIRE(on.size() == off.size());
	for (size_t frame = 0; frame < on.size(); ++frame)
	{
		CAPTURE(frame);
		CHECK(bgl::test::MaxChannelDelta(on[frame], off[frame]) == 0.0f);
	}
}

TEST_CASE(
	"An impostor hidden behind a wall is culled and drawn the frame it is revealed",
	"[culling][occlusion][impostor][render]")
{
	const Sequence on      = RenderSequence(true, Hidden::kImpostor);
	const Sequence off     = RenderSequence(false, Hidden::kImpostor);
	const Sequence without = RenderSequence(false, Hidden::kNothing);

	const auto ran = [](const Sequence& sequence, const std::string_view prefix) {
		return std::ranges::any_of(sequence.passes, [&](const std::string& name) {
			return name.starts_with(prefix);
		});
	};
	CHECK(ran(on, "Forward Impostor Phase 2"));
	CHECK_FALSE(ran(off, "Forward Impostor Phase 2"));

	// The premise: the wall hides the impostor at first -- the frame is the one without it -- and
	// the move reveals it by the end.
	CHECK(bgl::test::MaxChannelDelta(off.frames.front(), without.frames.front()) == 0.0f);
	CHECK(bgl::test::MaxChannelDelta(off.frames.back(), without.frames.back()) > 0.0f);

	REQUIRE(on.frames.size() == off.frames.size());
	for (size_t frame = 0; frame < on.frames.size(); ++frame)
	{
		CAPTURE(frame);
		CHECK(bgl::test::MaxChannelDelta(on.frames[frame], off.frames[frame]) == 0.0f);
	}
}
