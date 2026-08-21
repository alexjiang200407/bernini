#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/GoldenImage.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib/skeleton.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// That the overlay draws the skeleton the palette holds, where the palette says it is.
//
// No committed golden: the frames compared here are both rendered in this run, off against on, and
// what is asserted is luma at the projected position of a bone. That states *where* a bone was drawn,
// which is the overlay's whole claim -- a picture blessed as a golden would agree with itself even
// if every bone were a unit out.

namespace
{
	constexpr uint32_t c_Width      = 256;
	constexpr uint32_t c_Height     = 256;
	constexpr uint32_t c_BoneCount  = 3;
	constexpr uint32_t c_Frames     = 2;
	constexpr float    c_SampleRate = 30.0f;

	/** A three-bone chain up +Y, each child one unit above its parent. SkinnedPose_test's rig. */
	assetlib::Skeleton
	MakeChain()
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

	/** Frame 0 is the bind pose; frame 1 swings bone 1 by 90 degrees about +Z. */
	assetlib::AnimationSet
	MakeSwingClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

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
		clip.loop        = 0;  // one-shot, so a held frame is exactly the frame asked for
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/** One triangle carrying skin binding; the rig is the subject, not the geometry. */
	assetlib::BMesh
	MakeSkinnedTriangle()
	{
		constexpr uint16_t c_Stride = 12 + 8 + 8;

		auto mesh = assetlib::BMesh();
		mesh.vertexData.resize(size_t(3) * c_Stride);

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 3;
		meshlet.triangleCount  = 1;
		meshlet.boundingRadius = 4.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 3; ++v) mesh.meshletVertices.push_back(v);
		for (uint8_t t = 0; t < 3; ++t) mesh.meshletTriangles.push_back(t);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 3;
		submesh.layout.stride         = c_Stride;
		submesh.layout.attributes[0]  = { assetlib::VertexSemantic::kPosition,
			                              assetlib::VertexFormat::kFloat32x3,
			                              0 };
		submesh.layout.attributes[1]  = { assetlib::VertexSemantic::kJoints0,
			                              assetlib::VertexFormat::kUint16x4,
			                              12 };
		submesh.layout.attributes[2]  = { assetlib::VertexSemantic::kWeights0,
			                              assetlib::VertexFormat::kUnorm16x4,
			                              20 };
		submesh.vertexCount           = 3;
		submesh.meshletCount          = 1;
		submesh.material              = 0;
		submesh.aabbMin               = glm::vec3(-1.0f);
		submesh.aabbMax               = glm::vec3(3.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	glm::ivec2
	PixelOf(const bgl::Camera& camera, const glm::vec3& world)
	{
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(world, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return { static_cast<int>(std::lround((ndc.x * 0.5f + 0.5f) * float(c_Width))),
			     static_cast<int>(std::lround((ndc.y * -0.5f + 0.5f) * float(c_Height))) };
	}

	/** Mean luma of a small box centred on where `world` projects to. */
	float
	LumaAt(const std::string& png, const bgl::Camera& camera, const glm::vec3& world)
	{
		const glm::ivec2 px = PixelOf(camera, world);
		return bgl::test::MeanColor(png, px.x - 3, px.y - 3, 6, 6).Luma();
	}

	/** Mean blue of the same box: the overlay's own colour, which the lit rig has none of. */
	float
	BlueAt(const std::string& png, const bgl::Camera& camera, const glm::vec3& world)
	{
		const glm::ivec2 px = PixelOf(camera, world);
		return bgl::test::MeanColor(png, px.x - 3, px.y - 3, 6, 6).b;
	}
}

TEST_CASE("the bone overlay draws the skeleton the palette holds", "[skinned][bones][render]")
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
	REQUIRE(target != nullptr);

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
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	const auto skeleton = MakeChain();

	auto animations              = MakeSwingClip();
	animations.skeletonSignature = assetlib::skeletonSignature(skeleton);

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedTriangle(),
		0,
		materials,
		skeleton,
		animations,
		assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
	REQUIRE(geom.IsValid());

	auto camera = bgl::Camera();
	camera
		.LookAt(
			glm::vec3(0.0f, 1.0f, 8.0f),
			glm::vec3(0.0f, 1.0f, 7.0f),
			glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(60.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	auto* viewRaw = view->As<bgl::SceneView>();
	REQUIRE(viewRaw != nullptr);

	const auto shoot = [&](const std::string& path) {
		gfx->DrawFrame(target, job);
		gfx->ScreenshotPng(target, path);
	};

	SECTION("off by default, and off changes nothing about the frame")
	{
		CHECK_FALSE(target->IsBoneOverlayEnabled());

		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		REQUIRE(instance.IsValid());

		const std::string first  = "assets/golden/bone_overlay_off.got.png";
		const std::string second = "assets/golden/bone_overlay_off_again.got.png";
		shoot(first);
		shoot(second);

		// Two frames of the same still scene with the overlay off must be the same frame. This is
		// also what makes the comparison below mean anything: without it a difference could be the
		// renderer wobbling rather than the overlay arriving.
		CHECK(bgl::test::FrameDelta(first, second, 0, 0, int(c_Width), int(c_Height)) < 1e-6f);

		target->SetBoneOverlayEnabled(true);

		const std::string on = "assets/golden/bone_overlay_on.got.png";
		shoot(on);

		// And turning it on must change the frame. A toggle that quietly did nothing would satisfy
		// every assertion above.
		CHECK(bgl::test::FrameDelta(first, on, 0, 0, int(c_Width), int(c_Height)) > 1e-4f);
	}

	SECTION("turning it on puts bones where the rig's joints are")
	{
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		REQUIRE(instance.IsValid());

		const std::string off = "assets/golden/bone_overlay_bind_off.got.png";
		shoot(off);

		target->SetBoneOverlayEnabled(true);

		const std::string on = "assets/golden/bone_overlay_bind_on.got.png";
		shoot(on);

		// The rig stands at (0,0,0) -> (0,1,0) -> (0,2,0) in its bind pose, so the two bones below
		// the top joint are drawn between them. Their midpoints must have gained the overlay's blue.
		CHECK(BlueAt(on, camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.25f);
		CHECK(BlueAt(on, camera, glm::vec3(0.0f, 1.5f, 0.0f)) > 0.25f);

		CHECK(BlueAt(off, camera, glm::vec3(0.0f, 0.5f, 0.0f)) < 0.05f);
		CHECK(BlueAt(off, camera, glm::vec3(0.0f, 1.5f, 0.0f)) < 0.05f);

		// Nothing is drawn where the rig is not: a bone reaching sideways would mean the placement
		// transform or the roll basis is wrong.
		CHECK(LumaAt(on, camera, glm::vec3(-2.0f, 1.0f, 0.0f)) < 0.02f);
	}

	SECTION("the bones follow the clip, not the bind pose")
	{
		// Frame 1 swings bone 1 by 90 degrees about +Z, so the chain above it leaves +Y for -X.
		// This is the assertion that separates "drew a skeleton" from "drew the posed skeleton".
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
		REQUIRE(instance.IsValid());

		target->SetBoneOverlayEnabled(true);

		const std::string on = "assets/golden/bone_overlay_swung.got.png";
		shoot(on);

		// The swung bone runs from (0,1,0) to (-1,1,0); its midpoint carries the overlay.
		CHECK(BlueAt(on, camera, glm::vec3(-0.5f, 1.0f, 0.0f)) > 0.25f);

		// And it is no longer where the bind pose had it.
		CHECK(BlueAt(on, camera, glm::vec3(0.0f, 1.5f, 0.0f)) < 0.15f);
	}

	SECTION("the bones are drawn through the mesh, not behind it")
	{
		// The x-ray claim, and the reason the overlay has a depth buffer of its own. A quad parked
		// between the camera and the rig would hide the skeleton if the overlay tested scene depth.
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		REQUIRE(instance.IsValid());

		const auto blocker = scene->AddCubeGeom(pbr);
		REQUIRE(blocker.IsValid());

		// Parked between the camera and the rig, and wide enough to cover it.
		const auto wall = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 4.0f)) *
		                  glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, 4.0f, 0.5f));

		REQUIRE(view->CreateStaticMeshInstance(blocker, wall).IsValid());

		const std::string blocked = "assets/golden/bone_overlay_xray_off.got.png";
		shoot(blocked);

		// The claim is only worth making if the wall is really in the way: without this the section
		// would pass just as well with the blocker behind the rig, or missing altogether.
		REQUIRE(LumaAt(blocked, camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.05f);

		target->SetBoneOverlayEnabled(true);

		const std::string on = "assets/golden/bone_overlay_xray.got.png";
		shoot(on);

		CHECK(BlueAt(on, camera, glm::vec3(0.0f, 0.5f, 0.0f)) > 0.25f);
	}

	SECTION("a placement standing elsewhere takes its skeleton with it")
	{
		const auto world = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));

		const auto instance = view->CreateSkinnedMeshInstance(geom, world, { 0, 0.0f, 0.0f });
		REQUIRE(instance.IsValid());

		target->SetBoneOverlayEnabled(true);

		const std::string on = "assets/golden/bone_overlay_placed.got.png";
		shoot(on);

		CHECK(BlueAt(on, camera, glm::vec3(2.0f, 0.5f, 0.0f)) > 0.25f);
		CHECK(BlueAt(on, camera, glm::vec3(0.0f, 0.5f, 0.0f)) < 0.05f);
	}
}
