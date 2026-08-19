#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <assetlib_structs/Bounds.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

	// position(12) + normal(12) + uv(8) + tangent(16) + joints0(8) + weights0(8)
	constexpr uint16_t c_Stride = 64;

	void
	PutFloats(std::vector<std::byte>& bytes, size_t at, std::span<const float> values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(float));
	}

	void
	PutU16x4(std::vector<std::byte>& bytes, size_t at, std::span<const uint16_t> values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(uint16_t));
	}

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
		mesh.vertexData.assign(size_t(4) * c_Stride, std::byte{ 0 });

		for (uint32_t v = 0; v < 4; ++v)
		{
			const size_t base = size_t(v) * c_Stride;

			const std::array<float, 3> pos = { { positions[v].x, positions[v].y, positions[v].z } };
			const std::array<float, 3> normal = { { 0.0f, 0.0f, 1.0f } };
			const std::array<float, 2> uv  = { { positions[v].x + 0.5f, positions[v].y * 0.5f } };
			const std::array<float, 4> tan = { { 1.0f, 0.0f, 0.0f, 1.0f } };

			PutFloats(mesh.vertexData, base + 0, pos);
			PutFloats(mesh.vertexData, base + 12, normal);
			PutFloats(mesh.vertexData, base + 24, uv);
			PutFloats(mesh.vertexData, base + 32, tan);

			// One influence at full weight, the rest at zero -- unorm16 0xFFFF is exactly 1.0. With
			// `unboundTopEdge`, the two top vertices instead carry four zero weights: what an exporter
			// writes for a vertex it never assigned to a bone, which assetlib's CPU reference returns
			// unskinned rather than collapsed.
			const bool                    unbound = unboundTopEdge && v >= 2;
			const std::array<uint16_t, 4> joints  = { { bone[v], 0, 0, 0 } };
			const std::array<uint16_t, 4> weights = { { uint16_t(unbound ? 0 : 0xFFFF), 0, 0, 0 } };
			PutU16x4(mesh.vertexData, base + 48, joints);
			PutU16x4(mesh.vertexData, base + 56, weights);
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

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 6;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kNormal,
			                              assetlib::VertexFormat::kFloat32x3,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kTexCoord0,
			                              assetlib::VertexFormat::kFloat32x2,
			                              24 };
		submesh.layout.attributes[3]  = { assetlib::VertexSemantic::kTangent,
			                              assetlib::VertexFormat::kFloat32x4,
			                              32 };
		submesh.layout.attributes[4]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              48 };
		submesh.layout.attributes[5]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              56 };
		submesh.vertexCount           = 4;
		submesh.meshletCount          = 1;
		submesh.material              = 0;

		submesh.aabbMin = glm::vec3(-0.5f, 0.0f, 0.0f);
		submesh.aabbMax = glm::vec3(0.5f, 2.0f, 0.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	assetlib::Skeleton
	MakeTwoBoneRig()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_BoneCount; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	assetlib::AnimationSet
	MakeSwingClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);  // Rz(90), w first

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_BoneCount; ++b)
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
		MakeTwoBoneRig(),
		MakeSwingClip(),
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
		MakeTwoBoneRig(),
		MakeSwingClip(),
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
		MakeTwoBoneRig(),
		MakeSwingClip(),
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
		MakeTwoBoneRig(),
		MakeSwingClip(),
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
