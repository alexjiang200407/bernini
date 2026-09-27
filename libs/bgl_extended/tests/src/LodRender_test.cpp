#include "util/GoldenImage.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/LodLevel.h>
#include <bgl/RenderJob.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/LodSelectionDesc.h>
#include <bgl/types/SceneDesc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// The level the cull chose, drawn, and a change dissolved. Level 0 and level 1 of one placement are
// quads in the left and the right half of the frame, so what each covers is what each level drew:
// the near level alone, the far level alone, and between them a dissolve whose share moves from one
// to the other frame by frame. Temporal AA is off, so a frame is the pattern itself rather than the
// resolve's average of it -- the pattern is what this file pins.

namespace
{
	constexpr int c_Size = 64;

	// Where each quad lands at distance 2 under a 90-degree field of view: x in [16, 30] and
	// [34, 48], y in [16, 48]. The boxes sit inside those with a margin.
	constexpr int c_BoxY = 20, c_BoxH = 24, c_BoxW = 10;
	constexpr int c_LeftX = 18, c_RightX = 36;

	/** One quad of two triangles in the z = 0 plane, spanning x0..x1 and -1..1. */
	void
	AppendQuad(assetlib::BMesh& mesh, float x0, float x1)
	{
		constexpr uint16_t c_Stride = 12;

		const std::array<glm::vec3, 4> corners = {
			glm::vec3(x0, -1.0f, 0.0f),
			glm::vec3(x1, -1.0f, 0.0f),
			glm::vec3(x1, 1.0f, 0.0f),
			glm::vec3(x0, 1.0f, 0.0f),
		};
		const auto byteOffset = static_cast<uint32_t>(mesh.vertexData.size());
		mesh.vertexData.resize(byteOffset + sizeof(corners));
		std::memcpy(mesh.vertexData.data() + byteOffset, corners.data(), sizeof(corners));

		auto meshlet            = assetlib::Meshlet();
		meshlet.vertexOffset    = static_cast<uint32_t>(mesh.meshletVertices.size());
		meshlet.triangleOffset  = static_cast<uint32_t>(mesh.meshletTriangles.size());
		meshlet.vertexCount     = 4;
		meshlet.triangleCount   = 2;
		meshlet.boundingRadius  = 2.0f;
		const auto firstMeshlet = static_cast<uint32_t>(mesh.meshlets.size());
		mesh.meshlets.push_back(meshlet);
		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		for (const uint8_t index : { 0, 1, 2, 0, 2, 3 }) mesh.meshletTriangles.push_back(index);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 1;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.vertexByteOffset      = byteOffset;
		submesh.vertexCount           = 4;
		submesh.firstMeshlet          = firstMeshlet;
		submesh.meshletCount          = 1;
		submesh.material              = assetlib::c_InvalidIndex;
		submesh.aabbMin               = glm::vec3(x0, -1.0f, 0.0f);
		submesh.aabbMax               = glm::vec3(x1, 1.0f, 0.0f);
		mesh.submeshes.push_back(submesh);
	}

	/** Level 0 left of centre, level 1 right of it; level 0 while the placement spans 20 pixels. */
	assetlib::BMesh
	MakeSplitLevels()
	{
		auto mesh = assetlib::BMesh();
		AppendQuad(mesh, -1.0f, -0.1f);
		AppendQuad(mesh, 0.1f, 1.0f);
		mesh.meshes.push_back(
			assetlib::Mesh{ .firstSubmesh = 0, .submeshCount = 1, .nameOffset = 0, .lodCount = 2 });
		mesh.lods = { { 20.0f }, { 0.0f } };
		return mesh;
	}

	/**
	 * The placement stands at distance 2 and spans about 35 pixels, so a threshold scale of 4 puts
	 * it at level 1 and a scale of 1 at level 0 -- the level changes and the pixels do not move.
	 */
	struct SplitScene
	{
		bgl::GraphicsRef     gfx;
		bgl::SceneRef        scene;
		bgl::SceneViewRef    view;
		bgl::RenderTargetRef target;
		float                time = 0.0f;

		SplitScene()
		{
			auto opts           = bgl::GraphicsOptions();
			opts.shaderCacheDir = bgl::test::ShaderCacheDir();
			gfx                 = bgl::CreateGraphics(opts);
			REQUIRE(gfx != nullptr);

			scene = gfx->CreateScene(bgl::SceneDesc());
			view  = gfx->CreateSceneView(scene, 4);

			const auto geom = scene->AddStaticMeshGeom(MakeSplitLevels(), 0, {});
			REQUIRE(geom.IsValid());
			view->CreateStaticMeshInstance(
				geom,
				glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -2.0f)));

			auto targetDesc       = bgl::RenderTargetDesc();
			targetDesc.width      = c_Size;
			targetDesc.height     = c_Size;
			targetDesc.headless   = true;
			targetDesc.taaEnabled = false;
			target                = gfx->CreateRenderTarget(targetDesc);
		}

		void
		Select(float pixelScale)
		{
			auto desc       = bgl::LodSelectionDesc();
			desc.pixelScale = pixelScale;
			view->SetLodSelection(desc);
		}

		/** One frame 30 ms after the last, and each level's share of its box. */
		struct Shares
		{
			float near;
			float far;
		};

		Shares
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
				(std::filesystem::temp_directory_path() / ("bernini_lod_" + name + ".png"))
					.string();
			gfx->ScreenshotPng(target, path);
			return { bgl::test::MeanColor(path, c_LeftX, c_BoxY, c_BoxW, c_BoxH).r,
				     bgl::test::MeanColor(path, c_RightX, c_BoxY, c_BoxW, c_BoxH).r };
		}
	};
}

TEST_CASE("the level the cull chose is the level drawn", "[lod][render]")
{
	auto split = SplitScene();

	split.Select(4.0f);
	const auto far = split.Frame("far");
	CHECK(far.near == 0.0f);
	CHECK(far.far > 0.5f);

	split.Select(0.5f);
	for (int frame = 0; frame < 6; ++frame) split.Frame("settling");
	const auto near = split.Frame("near");
	CHECK(near.near == Catch::Approx(far.far).margin(0.01f));
	CHECK(near.far == 0.0f);
}

TEST_CASE("a change of level dissolves one level into the other", "[lod][render]")
{
	auto split = SplitScene();
	split.Select(4.0f);
	const float full = split.Frame("before").far;
	REQUIRE(full > 0.5f);

	// Nearer: level 0 dissolves in over five frames while level 1 dissolves out, each pixel covered
	// by exactly one of them.
	split.Select(1.0f);
	float previousNear = 0.0f;
	for (int frame = 1; frame <= 4; ++frame)
	{
		const auto  shares = split.Frame("dissolve" + std::to_string(frame));
		const float share  = 0.2f * static_cast<float>(frame);
		INFO("frame " << frame);
		CHECK(shares.near / full == Catch::Approx(share).margin(0.1f));
		CHECK(shares.far / full == Catch::Approx(1.0f - share).margin(0.1f));
		CHECK(shares.near > previousNear);
		previousNear = shares.near;
	}

	const auto after = split.Frame("after");
	CHECK(after.near == Catch::Approx(full).margin(0.01f));
	CHECK(after.far == 0.0f);
}
