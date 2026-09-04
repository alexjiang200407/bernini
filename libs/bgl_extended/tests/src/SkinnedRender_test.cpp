#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include "util/VertexPacking.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <bgl/Camera.h>
#include <bgl/GeomType.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/LayerType.h>
#include <bgl/MaterialHandle.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ratio>
#include <span>
#include <string>
#include <vector>

// That the skinned path draws what its palette says. The strongest check here is not a golden image
// but the static path: the same mesh, the same vertices, posed at bind pose, must rasterize to the
// same pixels. A golden would only bless whatever this code produced; the static path is an
// independent reference that was already correct before any of this existed.

namespace
{
	constexpr uint32_t c_Width      = 400;
	constexpr uint32_t c_Height     = 400;
	constexpr uint32_t c_BoneCount  = 2;
	constexpr float    c_SampleRate = 30.0f;

	// Frame 0 is the bind pose; frame 1 swings bone 1 by 90 degrees about +Z about its own origin at
	// (0,1,0), which takes the strip's top edge from +Y onto -X.
	constexpr uint32_t c_Frames = 2;

	// Every pose the swing below reaches, with room to spare. The strip's own bind-pose box does not
	// hold once the top edge rotates, and the geom culls by this -- see IScene::AddSkinnedMeshGeom.
	const auto c_StripPosedBounds =
		assetlib::Bounds{ glm::vec3(-3.0f, -3.0f, -3.0f), glm::vec3(3.0f, 3.0f, 3.0f) };

	/**
	 * A vertical strip from y = 0 to y = 2, two triangles. Its bottom vertices are bound entirely to
	 * bone 0 and its top ones entirely to bone 1, so the swing below is visible as the top edge moving
	 * while the bottom stays put -- a mesh bound wholly to one bone would render a rotation as a rigid
	 * transform and prove nothing about skinning.
	 */
	assetlib::BMesh
	MakeSkinnedStrip(bool unboundTopEdge = false)
	{
		const std::array<glm::vec3, 4> positions = { {
			{ -0.5f, 0.0f, 0.0f },
			{ 0.5f, 0.0f, 0.0f },
			{ -0.5f, 2.0f, 0.0f },
			{ 0.5f, 2.0f, 0.0f },
		} };
		const std::array<uint16_t, 4>  bone      = { { 0, 0, 1, 1 } };

		auto mesh = assetlib::BMesh();
		mesh.vertexData.assign(size_t(4) * bgl::test::c_SkinnedVertexStride, std::byte{ 0 });

		for (uint32_t v = 0; v < 4; ++v)
		{
			const size_t base = size_t(v) * bgl::test::c_SkinnedVertexStride;

			const std::array<float, 3> pos = { { positions[v].x, positions[v].y, positions[v].z } };
			const std::array<float, 3> normal = { { 0.0f, 0.0f, 1.0f } };
			const std::array<float, 2> uv  = { { positions[v].x + 0.5f, positions[v].y * 0.5f } };
			const std::array<float, 4> tan = { { 1.0f, 0.0f, 0.0f, 1.0f } };

			bgl::test::PutFloats(mesh.vertexData, base + 0, pos);
			bgl::test::PutFloats(mesh.vertexData, base + 12, normal);
			bgl::test::PutFloats(mesh.vertexData, base + 24, uv);
			bgl::test::PutFloats(mesh.vertexData, base + 32, tan);

			// One influence at full weight, the rest at zero -- unorm16 0xFFFF is exactly 1.0. With
			// `unboundTopEdge`, the two top vertices instead carry four zero weights: what an exporter
			// writes for a vertex it never assigned to a bone, which assetlib's CPU reference returns
			// unskinned rather than collapsed.
			const bool                    unbound = unboundTopEdge && v >= 2;
			const std::array<uint16_t, 4> joints  = { { bone[v], 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { uint16_t(unbound ? 0 : 0xFFFF), 0, 0, 0 } };
			bgl::test::PutU16x4(mesh.vertexData, base + 48, joints);
			bgl::test::PutU16x4(mesh.vertexData, base + 56, weights);
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingCenter = glm::vec3(0.0f, 1.0f, 0.0f);
		meshlet.boundingRadius = 4.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		// Meshlet-local indices, and uint8_t rather than a bare braced list: an int list narrows on
		// the way in, which MSVC refuses under the project's warning-as-error settings.
		const std::array<uint8_t, 6> tris = { { 0, 1, 2, 2, 1, 3 } };
		for (const uint8_t t : tris) mesh.meshletTriangles.push_back(t);

		auto submesh         = assetlib::Submesh();
		submesh.layout       = bgl::test::SkinnedVertexLayout();
		submesh.vertexCount  = 4;
		submesh.meshletCount = 1;
		submesh.material     = 0;

		submesh.aabbMin = glm::vec3(-0.5f, 0.0f, 0.0f);
		submesh.aabbMax = glm::vec3(0.5f, 2.0f, 0.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	assetlib::Skeleton
	MakeTwoBoneRig(uint32_t boneCount = c_BoneCount)
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < boneCount; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			// A binary tree, not a chain: the pose pass walks one barrier-synced level per depth, so a
			// chain of N bones costs N-1 serialized levels and is the worst rig of its size that
			// exists. A real rig branches. At the default two bones this is bone 0's child either
			// way, so every case below is unaffected.
			bone.parent     = i == 0 ? assetlib::c_InvalidIndex : (i - 1) / 2;
			bone.nameOffset = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	assetlib::AnimationSet
	MakeSwingClip(uint32_t boneCount = c_BoneCount)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = boneCount;

		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);  // Rz(90), w first

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < boneCount; ++b)
			{
				auto sample        = assetlib::Transform();
				sample.translation = glm::vec3(0.0f, b == 0 ? 0.0f : 1.0f, 0.0f);
				sample.rotation    = (f == 1 && b == 1) ? swing : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale       = glm::vec3(1.0f);
				set.samples.push_back(sample);
			}
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = c_Frames;
		clip.sampleRate  = c_SampleRate;
		clip.duration    = 1.0f / c_SampleRate;
		clip.loop        = 0;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/**
	 * The swing again with the rotation taken out: bone 1 slides along +X instead of turning.
	 *
	 * It exists for the fractional-frame comparison. The two pose sources blend differently by
	 * design -- the pose pass nlerps local rotations and then walks, the table lerps the two frames'
	 * finished skin matrices (ADR-10) -- and those disagree on a rotation. On a pure translation they
	 * cannot, so this isolates the table's addressing and blend from a difference that is not a bug.
	 */
	assetlib::AnimationSet
	MakeSlideClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_BoneCount; ++b)
			{
				auto sample = assetlib::Transform();
				sample.translation =
					glm::vec3((f == 1 && b == 1) ? 0.75f : 0.0f, b == 0 ? 0.0f : 1.0f, 0.0f);
				sample.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale    = glm::vec3(1.0f);
				set.samples.push_back(sample);
			}
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = c_Frames;
		clip.sampleRate  = c_SampleRate;
		clip.duration    = 1.0f / c_SampleRate;
		clip.loop        = 0;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	bgl::Camera
	StripCamera()
	{
		auto camera = bgl::Camera();
		camera
			.LookAt(
				glm::vec3(0.0f, 1.0f, 6.0f),
				glm::vec3(0.0f, 1.0f, 5.0f),
				glm::vec3(0.0f, 1.0f, 0.0f))
			.Perspective(
				glm::radians(60.0f),
				static_cast<float>(c_Width) / static_cast<float>(c_Height),
				0.5f,
				100.0f);
		return camera;
	}

	glm::ivec2
	PixelOf(const bgl::Camera& camera, const glm::vec3& world)
	{
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(world, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return { static_cast<int>(std::lround((ndc.x * 0.5f + 0.5f) * float(c_Width))),
			     static_cast<int>(std::lround((ndc.y * -0.5f + 0.5f) * float(c_Height))) };
	}

	float
	LumaAt(const std::string& png, const bgl::Camera& camera, const glm::vec3& world)
	{
		const glm::ivec2 px = PixelOf(camera, world);
		return bgl::test::MeanColor(png, px.x - 4, px.y - 4, 8, 8).Luma();
	}
}

TEST_CASE(
	"a skinned mesh at bind pose draws exactly what the static path draws",
	"[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// The identical BMesh down both paths. Same bytes, same material, same place on screen -- the
	// only difference is which pipeline decodes it.
	const auto staticGeom  = scene->AddStaticMeshGeom(MakeSkinnedStrip(), 0, materials);
	const auto skinnedGeom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);
	REQUIRE(staticGeom.IsValid());
	REQUIRE(skinnedGeom.IsValid());

	const auto* staticPng  = "assets/golden/skinned_static_ref.got.png";
	const auto* skinnedPng = "assets/golden/skinned_bind_pose.got.png";

	const auto staticInstance = view->CreateStaticMeshInstance(staticGeom, glm::mat4(1.0f));
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, staticPng);
	view->DeleteMeshInstance(staticInstance);

	// rate 0 holds frame 0, which is the bind pose: every skinning matrix is identity, so the vertex
	// the mesh shader emits must be the bind-pose vertex the static path emits, to the bit.
	view->CreateSkinnedMeshInstance(skinnedGeom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
	gfx->DrawFrame(target, job);
	gfx->ScreenshotPng(target, skinnedPng);

	// Over the whole frame, not a probe box: a difference anywhere -- a shifted vertex, a flipped
	// normal, a lost tangent -- has to show up.
	CHECK(bgl::test::FrameDelta(staticPng, skinnedPng, 0, 0, int(c_Width), int(c_Height)) < 1e-6f);

	SECTION("and the strip really is on screen, so the comparison is not of two empty frames")
	{
		CHECK(LumaAt(skinnedPng, job.camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.02f);
		CHECK(LumaAt(skinnedPng, job.camera, glm::vec3(0.0f, 1.5f, 0.0f)) > 0.02f);
		CHECK(LumaAt(skinnedPng, job.camera, glm::vec3(-2.0f, 1.5f, 0.0f)) < 0.01f);
	}
}

// The ceiling that used to sit at 192 bones was the pose pass's groupshared hierarchy array. It now
// composes in the palette, so what bounds a rig is the palette's own allocation -- but a walk that
// wrote past its slot, or read a parent another thread had not finished composing, would show up
// here as a strip in the wrong place rather than as a refusal.
TEST_CASE("a rig past the old groupshared ceiling poses correctly", "[skinned][render]")
{
	// Well past 192, and a chain rather than a fan: bone i parents bone i+1, so the walk runs one
	// barrier-separated level per bone. The strip is bound to bones 0 and 1 as always; the rest are
	// there to be walked.
	constexpr uint32_t c_DeepBones = 300;

	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	const auto deepGeom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(c_DeepBones), MakeSwingClip(c_DeepBones)),
		c_StripPosedBounds);

	// The refusal this used to hit is gone, and that is half of what the test is for.
	REQUIRE(deepGeom.IsValid());

	const auto staticGeom = scene->AddStaticMeshGeom(MakeSkinnedStrip(), 0, materials);
	REQUIRE(staticGeom.IsValid());

	// The same strip on the rig this suite already trusts, to measure the deep one against.
	const auto shallowGeom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);
	REQUIRE(shallowGeom.IsValid());

	const auto capture = [&](const char* png, bgl::GeomHandle geom, float phase) {
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, phase, 0.0f });
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);
		view->DeleteMeshInstance(instance);
		return png;
	};

	SECTION("posed, it matches the two-bone rig the rest of this suite measures against")
	{
		// phase 1 holds the swung frame, so the walk has to compose bone 1 through bone 0 rather
		// than hand back a local transform. Bones 2 and up weight no vertex: what they add is 298
		// more barrier-separated levels for that composition to survive.
		const auto* shallowPng =
			capture("assets/golden/skinned_deep_shallow_ref.got.png", shallowGeom, 1.0f);
		const auto* deepPng = capture("assets/golden/skinned_deep_swing.got.png", deepGeom, 1.0f);

		CHECK(
			bgl::test::FrameDelta(shallowPng, deepPng, 0, 0, int(c_Width), int(c_Height)) < 1e-6f);

		// And really swung, not two bind poses matching each other: the top edge has left +Y and
		// arrived on -X, which is only true if bone 1 composed through bone 0.
		CHECK(LumaAt(deepPng, job.camera, glm::vec3(0.0f, 1.8f, 0.0f)) < 0.01f);
		CHECK(LumaAt(deepPng, job.camera, glm::vec3(-0.8f, 1.0f, 0.0f)) > 0.02f);
	}

	SECTION("at bind pose it draws what the static path draws")
	{
		const auto staticInstance = view->CreateStaticMeshInstance(staticGeom, glm::mat4(1.0f));
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, "assets/golden/skinned_deep_static_ref.got.png");
		view->DeleteMeshInstance(staticInstance);

		const auto* deepPng =
			capture("assets/golden/skinned_deep_bind_pose.got.png", deepGeom, 0.0f);

		CHECK(
			bgl::test::FrameDelta(
				"assets/golden/skinned_deep_static_ref.got.png",
				deepPng,
				0,
				0,
				int(c_Width),
				int(c_Height)) < 1e-6f);

		CHECK(LumaAt(deepPng, job.camera, glm::vec3(0.0f, 1.5f, 0.0f)) > 0.02f);
	}
}

TEST_CASE("a posed skinned mesh moves the bones' vertices and nothing else", "[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);
	REQUIRE(geom.IsValid());

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// Held on frame 1: bone 1 swung 90 degrees about +Z, bone 0 untouched.
	view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
	gfx->DrawFrame(target, job);

	const auto* png = "assets/golden/skinned_swing.got.png";
	gfx->ScreenshotPng(target, png);

	// The bottom half is bound to bone 0, which did not move.
	CHECK(LumaAt(png, job.camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.02f);

	// The top half is bound to bone 1, and swung onto -X about (0,1,0): it is gone from where the
	// bind pose put it, and present where the rotation sends it.
	CHECK(LumaAt(png, job.camera, glm::vec3(0.0f, 1.8f, 0.0f)) < 0.01f);
	CHECK(LumaAt(png, job.camera, glm::vec3(-0.8f, 1.0f, 0.0f)) > 0.02f);
}

TEST_CASE("a vertex bound to no bone keeps its bind pose", "[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(true),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);
	REQUIRE(geom.IsValid());

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// Frame 1 swings bone 1. The top vertices name it, but carry no weight at all, so the swing must
	// not reach them: four zero weights sum to a zero matrix, and a path that used it verbatim would
	// collapse them onto the origin rather than leaving them at the bind pose.
	view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
	gfx->DrawFrame(target, job);

	const auto* png = "assets/golden/skinned_unbound.got.png";
	gfx->ScreenshotPng(target, png);

	// Still standing where the bind pose put it, top edge included.
	CHECK(LumaAt(png, job.camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.02f);
	CHECK(LumaAt(png, job.camera, glm::vec3(0.0f, 1.8f, 0.0f)) > 0.02f);

	// And not swung onto -X, which is where the same frame put it when the weights were real.
	CHECK(LumaAt(png, job.camera, glm::vec3(-0.8f, 1.0f, 0.0f)) < 0.01f);
}

TEST_CASE(
	"an animating skinned mesh writes motion vectors and a held one does not",
	"[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material           = bgl::PbrMaterialDesc();
	material.metallicFactor = 0.0f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// The camera never moves in either case, so any velocity on screen came from the pose. Both are
	// driven two frames, because prevTime equals time on the first one by construction.
	const auto peakVelocity = [&](float rate) {
		auto localView = gfx->CreateSceneView(scene, 4);
		bgl::test::ApplyEnvironment(scene.Get(), localView.Get());

		auto localJob = job;
		localJob.view = localView;
		localView->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, rate });

		localJob.time = 0.0f;
		gfx->DrawFrame(target, localJob);

		localJob.time = 1.0f / c_SampleRate;
		gfx->DrawFrame(target, localJob);

		const auto velocity =
			bgl::test::ReadMotionVectors(gfx.Get(), target.Get(), c_Width, c_Height);

		float peak = 0.0f;
		for (const glm::vec2& v : velocity)
		{
			peak = std::max(peak, glm::length(v));
		}
		return peak;
	};

	const float animating = peakVelocity(1.0f);
	const float held      = peakVelocity(0.0f);

	// A whole frame of a 90-degree swing is a large screen-space move; the exact magnitude is a
	// function of the camera, so this asserts the thing that matters -- that it is written at all,
	// and that holding the pose writes nothing.
	CHECK(animating > 1e-3f);
	CHECK(held < 1e-5f);
}

/**
 * Blending on the skinned tier, and the decision behind it: the depth-sorted list holds every tier
 * at once and is drawn by one pipeline whose geometry stage branches per instance, so a blended rig
 * takes its place among blended static geometry by depth rather than by tier.
 *
 * The static path is the reference again. A blended strip at bind pose is the same surface whichever
 * pipeline posed it, so any arrangement that swaps one for the other must composite to the same
 * pixels -- and swapping the two panes' depths must not, or the comparison proves nothing.
 */
TEST_CASE("a blended skinned mesh sorts among blended static geometry", "[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 16;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 8192;
	sceneDesc.initialIndices              = 128;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 8);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	const auto blendMaterial = [&](const glm::vec3& tint) {
		auto desc            = bgl::PbrMaterialDesc();
		desc.baseColorFactor = glm::vec4(tint, 0.5f);
		desc.metallicFactor  = 0.0f;
		desc.roughnessFactor = 0.5f;
		desc.layerType       = bgl::LayerType::kBlend;
		return std::array<bgl::MaterialHandle, 1>{ { scene->CreatePbrMaterial(desc) } };
	};

	const auto red  = blendMaterial(glm::vec3(0.9f, 0.1f, 0.1f));
	const auto blue = blendMaterial(glm::vec3(0.1f, 0.1f, 0.9f));

	const auto staticGeom = [&](std::span<const bgl::MaterialHandle> materials) {
		return scene->AddStaticMeshGeom(MakeSkinnedStrip(), 0, materials);
	};
	const auto skinnedGeom = [&](std::span<const bgl::MaterialHandle> materials) {
		return scene->AddSkinnedMeshGeom(
			MakeSkinnedStrip(),
			0,
			materials,
			scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
			c_StripPosedBounds);
	};

	const auto staticRed   = staticGeom(red);
	const auto staticBlue  = staticGeom(blue);
	const auto skinnedRed  = skinnedGeom(red);
	const auto skinnedBlue = skinnedGeom(blue);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// The two panes are the same strip a unit apart along the view axis, so they overlap on screen
	// and the near one blends over the far one.
	constexpr float c_Near = 0.0f;
	constexpr float c_Far  = -1.0f;

	const auto at = [](float z) {
		return glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, z));
	};

	// rate 0 holds frame 0, the bind pose, which is the static geometry vertex for vertex.
	const auto place = [&](bgl::GeomHandle geom, float z) {
		return geom.geomType == bgl::GeomType::kSkinnedMesh ?
		           view->CreateSkinnedMeshInstance(geom, at(z), { 0, 0.0f, 0.0f }) :
		           view->CreateStaticMeshInstance(geom, at(z));
	};

	const auto render = [&](const char* png, bgl::GeomHandle nearGeom, bgl::GeomHandle farGeom) {
		const auto nearInstance = place(nearGeom, c_Near);
		const auto farInstance  = place(farGeom, c_Far);

		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);

		view->DeleteMeshInstance(nearInstance);
		view->DeleteMeshInstance(farInstance);
		return std::string(png);
	};

	const auto whole = [&](const std::string& a, const std::string& b) {
		return bgl::test::FrameDelta(a, b, 0, 0, int(c_Width), int(c_Height));
	};

	const auto reference =
		render("assets/golden/blend_sort_reference.got.png", staticRed, staticBlue);

	// The control. Red over blue and blue over red are different images, so an equality below is a
	// statement about ordering and not about two panes that happen to composite alike.
	const auto swapped = render("assets/golden/blend_sort_swapped.got.png", staticBlue, staticRed);
	REQUIRE(whole(reference, swapped) > 1e-4f);

	// Skinned in front of a static pane, then behind one. Drawing the tiers as separate passes would
	// break exactly one of these, whichever order the passes ran in.
	const auto skinnedNear =
		render("assets/golden/blend_sort_skinned_near.got.png", skinnedRed, staticBlue);
	CHECK(whole(reference, skinnedNear) < 1e-6f);

	const auto skinnedFar =
		render("assets/golden/blend_sort_skinned_far.got.png", staticRed, skinnedBlue);
	CHECK(whole(reference, skinnedFar) < 1e-6f);
}

/**
 * The outline mask on an animated instance. It draws through the same tier-branching geometry stage
 * the transparent phase does, so a selected rig contours the pose it is drawn in -- where before it
 * contoured the bind pose its vertex bytes hold, whatever the forward pass had put on screen.
 *
 * Selecting changes nothing but the outline, so the difference between the two frames *is* the
 * outline, whatever colour it is drawn in. That makes the negative probe the sharp one: a band above
 * the bind pose's top edge would say the mask never posed.
 */
TEST_CASE("a selected skinned instance contours its pose", "[skinned][selection][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
		c_StripPosedBounds);
	REQUIRE(geom.IsValid());

	// rate 1 at frame 1's time: bone 1 has swung 90 degrees, which carries the strip's top edge from
	// (0, 2) onto x = -1. The two edges the probes sit on are a half unit clear of each other's
	// shape, so neither box can see the other's band.
	const auto instance = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 1.0f });

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
	job.time     = 1.0f / c_SampleRate;

	const auto capture = [&](const std::string& path) {
		// Two frames: the first uploads and presents, the screenshot reads the last presented.
		gfx->DrawFrame(target, job);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
		return path;
	};

	const auto off = capture("assets/golden/skinned_outline_off.got.png");
	view->SetSubmeshSelected(instance, 0, true);
	const auto on = capture("assets/golden/skinned_outline_on.got.png");

	// A box straddling a silhouette edge: whatever the two frames differ by there is the band.
	const auto bandAt = [&](const glm::vec3& world) {
		const glm::ivec2 px = PixelOf(job.camera, world);
		return bgl::test::FrameDelta(off, on, px.x - 8, px.y - 8, 16, 16);
	};

	const float posed    = bandAt(glm::vec3(-1.0f, 1.0f, 0.0f));
	const float bindPose = bandAt(glm::vec3(0.0f, 2.0f, 0.0f));

	INFO("outline energy: " << posed << " at the swung edge, " << bindPose << " at the bind one");

	CHECK(posed > 1e-3f);
	CHECK(bindPose < 1e-5f);
}

TEST_CASE("an instance on its rig's table draws what the pose pass draws", "[skinned][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;

	auto scene = gfx->CreateScene(sceneDesc);
	auto view  = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f);
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.5f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = StripCamera();
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// One geom on one rig, drawn twice: the only difference between the two frames is where the
	// vertex stage read the pose from. The instances are placed one at a time so each frame holds
	// exactly one.
	const auto drawAs = [&](const assetlib::AnimationSet& clips,
	                        bgl::PoseSource               source,
	                        float                         phase,
	                        const char*                   png) {
		const auto geom = scene->AddSkinnedMeshGeom(
			MakeSkinnedStrip(),
			0,
			materials,
			scene->AddRig(MakeTwoBoneRig(), clips),
			c_StripPosedBounds);
		REQUIRE(geom.IsValid());

		auto desc   = bgl::SkinnedInstanceDesc();
		desc.clip   = 0;
		desc.phase  = phase;
		desc.rate   = 0.0f;  // holds `phase` under any clock
		desc.source = source;

		const auto instance = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), desc);
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, png);
		view->DeleteMeshInstance(instance);
	};

	const auto* palettePng = "assets/golden/pose_source_palette.got.png";
	const auto* tablePng   = "assets/golden/pose_source_table.got.png";

	SECTION("at every frame of the clip")
	{
		// Whole frames, where the table holds the pose exactly and no blend is involved either way.
		// The two must agree to the bit: same walk, same inverse binds, same vertex bytes.
		for (uint32_t frame = 0; frame < c_Frames; ++frame)
		{
			drawAs(MakeSwingClip(), bgl::PoseSource::kPerInstance, float(frame), palettePng);
			drawAs(MakeSwingClip(), bgl::PoseSource::kBoneAnimTable, float(frame), tablePng);

			CHECK(
				bgl::test::FrameDelta(palettePng, tablePng, 0, 0, int(c_Width), int(c_Height)) <
				1e-6f);
		}
	}

	SECTION("and between two frames of a clip that only translates")
	{
		// See MakeSlideClip: the two sources blend differently, and on a translation that difference
		// is zero, so a mismatch here is the table's addressing or its blend rather than ADR-10.
		drawAs(MakeSlideClip(), bgl::PoseSource::kPerInstance, 0.5f, palettePng);
		drawAs(MakeSlideClip(), bgl::PoseSource::kBoneAnimTable, 0.5f, tablePng);

		CHECK(
			bgl::test::FrameDelta(palettePng, tablePng, 0, 0, int(c_Width), int(c_Height)) < 1e-6f);
	}

	SECTION("and a moving one writes motion vectors, where a held one writes none")
	{
		const auto geom = scene->AddSkinnedMeshGeom(
			MakeSkinnedStrip(),
			0,
			materials,
			scene->AddRig(MakeTwoBoneRig(), MakeSwingClip()),
			c_StripPosedBounds);

		// The camera never moves, so any velocity on screen came from the pose -- which on this path
		// is a second read of the table at prevTime rather than a second palette. A fresh view each
		// time, and two frames, because prevTime equals time on the first by construction.
		const auto peakVelocity = [&](float rate) {
			auto localView = gfx->CreateSceneView(scene, 4);
			bgl::test::ApplyEnvironment(scene.Get(), localView.Get());

			auto localJob = job;
			localJob.view = localView;

			auto desc   = bgl::SkinnedInstanceDesc();
			desc.rate   = rate;
			desc.source = bgl::PoseSource::kBoneAnimTable;
			localView->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), desc);

			localJob.time = 0.0f;
			gfx->DrawFrame(target, localJob);

			localJob.time = 1.0f / c_SampleRate;
			gfx->DrawFrame(target, localJob);

			const auto velocity =
				bgl::test::ReadMotionVectors(gfx.Get(), target.Get(), c_Width, c_Height);

			float peak = 0.0f;
			for (const glm::vec2& v : velocity)
			{
				peak = std::max(peak, glm::length(v));
			}
			return peak;
		};

		CHECK(peakVelocity(1.0f) > 1e-3f);
		CHECK(peakVelocity(0.0f) < 1e-5f);
	}
}

// Hidden: it spawns thousands of instances and is a measurement rather than an assertion. Run it by
// hand -- `just run bgl_extended_tests -- "[.posetiming]"` -- and read the numbers off the warning it prints.
//
// The two sources on the identical mesh, rig and clip, so the only difference is where each vertex
// reads its pose. It is a throughput number over a whole frame and not a per-stage one -- the RHI
// has no timestamp query -- so read it as what a crowd costs, never as what one stage costs.
TEST_CASE("what a crowd costs on each pose source", "[.posetiming]")
{
	constexpr uint32_t c_Instances      = 2000;
	constexpr uint32_t c_MeasuredFrames = 30;

	// A crowd rig's bone count, not the two-bone fixture's. What the table removes is a pose pass
	// costing instances x bones, so a rig with two bones has nearly nothing to remove and the two
	// sources measure the same -- which says more about the fixture than about the tier.
	constexpr uint32_t c_CrowdBones = 64;

	auto opts           = bgl::GraphicsOptions();
	opts.shaderCacheDir = bgl::test::ShaderCacheDir();

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = gfx->CreateRenderTarget(targetDesc);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 8;
	sceneDesc.initialMeshlets             = 64;
	sceneDesc.initialSubmeshes            = 8;
	sceneDesc.initialVertexBufferByteSize = 65536;
	sceneDesc.initialIndices              = 1024;
	sceneDesc.initialPbrMaterials         = 8;

	auto scene = gfx->CreateScene(sceneDesc);

	auto material           = bgl::PbrMaterialDesc();
	material.metallicFactor = 0.0f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	// The strip still weights its vertices to bones 0 and 1, which is what a real rig does too: a
	// vertex touches at most four of however many the rig has. The rest are posed and never read,
	// and paying for them per instance per frame is exactly the cost the table exists to remove.
	const assetlib::Skeleton skeleton = MakeTwoBoneRig(c_CrowdBones);

	// The walk runs one barrier-synced level per depth, so this is what the pose pass is really
	// charged in -- reported beside the bone count, because the same 64 bones as a chain would cost
	// ten times the levels.
	uint32_t maxDepth = 0;
	{
		auto depth = std::vector<uint32_t>(skeleton.bones.size(), 0);
		for (size_t i = 1; i < skeleton.bones.size(); ++i)
		{
			depth[i] = depth[skeleton.bones[i].parent] + 1;
			maxDepth = std::max(maxDepth, depth[i]);
		}
	}

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedStrip(),
		0,
		materials,
		scene->AddRig(skeleton, MakeSwingClip(c_CrowdBones)),
		c_StripPosedBounds);
	REQUIRE(geom.IsValid());

	const auto msPerFrame = [&](bgl::PoseSource source) {
		auto view = gfx->CreateSceneView(scene, c_Instances + 8);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.camera   = StripCamera();
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		for (uint32_t i = 0; i < c_Instances; ++i)
		{
			auto desc   = bgl::SkinnedInstanceDesc();
			desc.phase  = float(i % 17);  // staggered, as a crowd is
			desc.rate   = 1.0f;
			desc.source = source;

			const float x = float(i % 50) * 0.4f - 10.0f;
			const float y = float(i / 50) * 0.4f - 8.0f;

			view->CreateSkinnedMeshInstance(
				geom,
				glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)),
				desc);
		}

		// One frame to fill the table and warm every PSO, then the measured run.
		job.time = 0.0f;
		gfx->DrawFrame(target, job);

		const auto start = std::chrono::steady_clock::now();
		for (uint32_t f = 0; f < c_MeasuredFrames; ++f)
		{
			job.time = float(f) / 30.0f;
			gfx->DrawFrame(target, job);
		}

		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
		           .count() /
		       double(c_MeasuredFrames);
	};

	const double perInstance = msPerFrame(bgl::PoseSource::kPerInstance);
	const double table       = msPerFrame(bgl::PoseSource::kBoneAnimTable);

	WARN(
		std::format(
			"crowd timing: {} instances x {} bones (depth {}) x {} frames | per-instance {:.2f} | "
			"table {:.2f} ms/frame",
			c_Instances,
			c_CrowdBones,
			maxDepth,
			c_MeasuredFrames,
			perInstance,
			table));

	CHECK(perInstance > 0.0);
	CHECK(table > 0.0);
}
