#include "gfx/GraphicsBase.h"
#include "scene/SceneView.h"
#include "util/GoldenImage.h"
#include "util/PaletteReadback.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/Camera.h>
#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/RenderJob.h>
#include <bgl/RigHandle.h>
#include <bgl/Viewport.h>
#include <bgl/glm.h>
#include <bgl/types/BlobShadowDesc.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl/types/PbrMaterialDesc.h>
#include <bgl/types/SceneDesc.h>
#include <bgl_common/idl/Constants.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <vector>

/**
 * The foot shadows, proven twice: where the pose pass says each sole stands, read straight off the
 * palette arena, and what the decal then draws under each one, read off the frame.
 *
 * The rig is a pelvis with two legs, feet pointing +Z, posed by a still clip at rate 0. The
 * right leg can be lifted in the clip, which is what a foot mid-stride looks like to everything
 * downstream of the pose.
 */

namespace
{
	constexpr uint32_t c_Pelvis = 0;
	constexpr uint32_t c_Bones  = 9;
	constexpr uint32_t c_Legs   = 2;
	constexpr uint32_t c_Frames = 2;

	constexpr float c_FootHeight = 0.1f;
	constexpr float c_FootLength = 0.2f;
	constexpr float c_Stance     = 0.3f;

	// hip, knee, ankle and toe of each leg, left (-X) then right (+X). The knees are carried
	// forward so each chain has a bend plane and reach to spare: a planted right foot lifted by the
	// clip has to straighten a tenth of a unit to meet the ground.
	glm::vec3
	BindOf(uint32_t bone)
	{
		if (bone == c_Pelvis)
			return glm::vec3(0.0f, 1.0f + c_FootHeight, 0.0f);

		const float    x     = bone <= 4 ? -c_Stance : c_Stance;
		const uint32_t joint = (bone - 1) % 4;

		const std::array<glm::vec3, 4> chain = { {
			glm::vec3(x, 1.0f + c_FootHeight, 0.0f),
			glm::vec3(x, 0.5f + c_FootHeight, 0.25f),
			glm::vec3(x, c_FootHeight, 0.0f),
			glm::vec3(x, c_FootHeight, c_FootLength),
		} };
		return chain[joint];
	}

	uint32_t
	ParentOf(uint32_t bone)
	{
		if (bone == c_Pelvis)
			return assetlib::c_InvalidIndex;
		return (bone - 1) % 4 == 0 ? c_Pelvis : bone - 1;
	}

	assetlib::Transform
	LocalOf(uint32_t bone)
	{
		const glm::vec3 parent = bone == c_Pelvis ? glm::vec3(0.0f) : BindOf(ParentOf(bone));
		return { BindOf(bone) - parent, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
	}

	assetlib::Skeleton
	MakeTwoLegRig()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_Bones; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = LocalOf(i);
			bone.inverseBind = glm::translate(glm::mat4(1.0f), -BindOf(i));
			bone.parent      = ParentOf(i);
			bone.nameOffset  = skeleton.stringPool.add(std::format("Bone{}", i));
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/** The bind pose held still, with the right hip raised `rightLift` off it. */
	assetlib::AnimationSet
	MakeStance(float rightLift)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_Bones;
		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_Bones; ++b)
			{
				assetlib::Transform sample = LocalOf(b);
				if (b == 5)
					sample.translation.y += rightLift;
				set.samples.push_back(sample);
			}
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = c_Frames;
		clip.sampleRate  = 30.0f;
		clip.duration    = 1.0f / 30.0f;
		clip.loop        = 0;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);
		return set;
	}

	bgl::FootPlantDesc
	MakeLegs()
	{
		auto plant = bgl::FootPlantDesc();
		for (uint32_t leg = 0; leg < c_Legs; ++leg)
		{
			auto chain       = bgl::FootPlantLegDesc();
			chain.hip        = 1 + leg * 4;
			chain.knee       = 2 + leg * 4;
			chain.ankle      = 3 + leg * 4;
			chain.toe        = 4 + leg * 4;
			chain.solePoint  = glm::vec3(0.0f, -c_FootHeight, 0.0f);
			chain.soleNormal = glm::vec3(0.0f, 1.0f, 0.0f);
			plant.legs.push_back(chain);
		}
		plant.plantWeights = std::vector<uint8_t>(size_t(c_Frames) * c_Legs, 255);
		return plant;
	}

	/** One degenerate triangle carrying skin binding: nothing to see, the rig is the subject. */
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
		submesh.aabbMax               = glm::vec3(2.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}

	/** A device, a flat white ground at y = 0 drawn as a static receiver, and the two-leg rig. */
	struct FootScene
	{
		bgl::GraphicsRef        gfx;
		bgl::SceneRef           scene;
		bgl::SceneViewRef       view;
		bgl::GeomHandle         geom;
		bgl::MeshInstanceHandle ground;
	};

	FootScene
	MakeFootScene(float rightLift, bool plantFeet, const bgl::FootPlantDesc& legs = MakeLegs())
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto result = FootScene();
		result.gfx  = bgl::CreateGraphics(opts);
		REQUIRE(result.gfx != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 16;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 8192;
		sceneDesc.initialIndices              = 256;
		sceneDesc.initialPbrMaterials         = 4;

		result.scene = result.gfx->CreateScene(sceneDesc);
		result.scene->SetGround(bgl::GroundPlaneDesc());
		result.scene->SetFootPlanting(plantFeet);

		result.view = result.gfx->CreateSceneView(result.scene, 8);
		bgl::test::ApplyEnvironment(result.scene.Get(), result.view.Get());

		auto whiteDesc            = bgl::PbrMaterialDesc();
		whiteDesc.baseColorFactor = glm::vec4(1.0f);
		whiteDesc.metallicFactor  = 0.0f;
		whiteDesc.roughnessFactor = 1.0f;
		const auto white          = result.scene->CreatePbrMaterial(whiteDesc);

		// The plane geoms are authored in XY; this lays one flat with its normal up.
		const auto groundGeom = result.scene->AddPlaneGeom(1, 1, 12.0f, 12.0f, white);
		result.ground         = result.view->CreateStaticMeshInstance(
			groundGeom,
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));

		const std::array<bgl::MaterialHandle, 1> materials = { { white } };

		const bgl::RigHandle rig =
			result.scene->AddRig(MakeTwoLegRig(), MakeStance(rightLift), legs);
		REQUIRE(rig.IsValid());

		result.geom = result.scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			rig,
			assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
		REQUIRE(result.geom.IsValid());
		return result;
	}

	/** Where each leg's heel and ball stand, world space, as the pose pass last wrote them. */
	std::vector<glm::vec4>
	ReadSoles(const FootScene& feet, bgl::MeshInstanceHandle instance)
	{
		auto* gfxBase = feet.gfx->As<bgl::GraphicsBase>();
		auto* viewRaw = feet.view->As<bgl::SceneView>();
		REQUIRE(gfxBase != nullptr);
		REQUIRE(viewRaw != nullptr);

		const uint32_t poses = 2 * bgl::idl::cFloat4sPerBone * c_Bones;
		return bgl::test::ReadPalette(
				   gfxBase,
				   viewRaw,
				   bgl::test::PaletteBaseOf(viewRaw, instance) + poses,
				   bgl::idl::cFloat4sPerSole * c_Legs)
		    .rows;
	}

	void
	CheckNear(const glm::vec4& actual, const glm::vec3& expected)
	{
		bgl::test::CheckNear(glm::vec3(actual), expected);
	}
}

TEST_CASE(
	"the pose pass stands each foot's sole where the drawn pose puts it",
	"[blobshadow][skinned][pose][render]")
{
	constexpr float c_Lift = 0.1f;
	const auto      where  = glm::vec3(2.0f, 0.0f, 3.0f);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
	targetDesc.headless = true;

	auto job     = bgl::RenderJob();
	job.viewport = bgl::Viewport(64.0f, 64.0f);

	SECTION("unplanted, a lifted foot's sole is lifted with it, in world space")
	{
		const FootScene feet     = MakeFootScene(c_Lift, false);
		const auto      instance = feet.view->CreateSkinnedMeshInstance(
			feet.geom,
			glm::translate(glm::mat4(1.0f), where),
			bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });

		auto target = feet.gfx->CreateRenderTarget(targetDesc);
		job.view    = feet.view;
		feet.gfx->DrawFrame(target, job);

		const std::vector<glm::vec4> soles = ReadSoles(feet, instance);
		REQUIRE(soles.size() == bgl::idl::cFloat4sPerSole * c_Legs);

		CheckNear(soles[0], where + glm::vec3(-c_Stance, 0.0f, 0.0f));
		CheckNear(soles[1], where + glm::vec3(-c_Stance, 0.0f, c_FootLength));
		CheckNear(soles[2], where + glm::vec3(c_Stance, c_Lift, 0.0f));
		CheckNear(soles[3], where + glm::vec3(c_Stance, c_Lift, c_FootLength));
	}

	SECTION("planted, the sole is read after the plant put the foot down")
	{
		const FootScene feet     = MakeFootScene(c_Lift, true);
		const auto      instance = feet.view->CreateSkinnedMeshInstance(
			feet.geom,
			glm::mat4(1.0f),
			bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });

		auto target = feet.gfx->CreateRenderTarget(targetDesc);
		job.view    = feet.view;
		feet.gfx->DrawFrame(target, job);

		const std::vector<glm::vec4> soles = ReadSoles(feet, instance);
		REQUIRE(soles.size() == bgl::idl::cFloat4sPerSole * c_Legs);

		// The clip held the right foot a tenth up; weight 255 says it stands, and the plant seats it.
		for (size_t point = 0; point < soles.size(); ++point)
		{
			INFO("sole point " << point);
			CHECK(std::abs(soles[point].y) < 1e-3f);
		}
	}
}

TEST_CASE(
	"a foot's shadow is dark where the foot stands and fades where it is lifted",
	"[blobshadow][skinned][render]")
{
	constexpr uint32_t c_Width  = 800;
	constexpr uint32_t c_Height = 600;

	// A tenth up under a fade height of three tenths: the lifted foot's shadow is at two thirds.
	constexpr float c_Lift = 0.1f;

	const FootScene feet     = MakeFootScene(c_Lift, false);
	const auto      instance = feet.view->CreateSkinnedMeshInstance(
		feet.geom,
		glm::mat4(1.0f),
		bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = static_cast<int>(c_Width);
	targetDesc.height   = static_cast<int>(c_Height);
	targetDesc.headless = true;
	auto target         = feet.gfx->CreateRenderTarget(targetDesc);

	// Oblique, as a game camera is: looking straight down, the static depth barely changes from one
	// row to the next, and the decal's facing test reads a quantized step as a wall.
	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 3.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(30.0f),
			static_cast<float>(c_Width) / static_cast<float>(c_Height),
			0.5f,
			100.0f);

	auto job     = bgl::RenderJob();
	job.view     = feet.view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	// The heel sits on the capsule's spine, so a box around it is inside the shadow's core.
	constexpr int c_Box = 8;
	const auto    boxAt = [&](const glm::vec3& world) {
		const glm::vec4 clip = camera.GetViewProjection() * glm::vec4(world, 1.0f);
		const glm::vec2 ndc  = glm::vec2(clip) / clip.w;
		return glm::ivec2(
			static_cast<int>((ndc.x * 0.5f + 0.5f) * c_Width) - c_Box / 2,
			static_cast<int>((0.5f - ndc.y * 0.5f) * c_Height) - c_Box / 2);
	};
	const glm::ivec2 left  = boxAt(glm::vec3(-c_Stance, 0.0f, 0.0f));
	const glm::ivec2 right = boxAt(glm::vec3(c_Stance, 0.0f, 0.0f));

	struct Lumas
	{
		float left  = 0.0f;
		float right = 0.0f;
	};
	const auto sample = [&](const char* name) {
		const auto path =
			(std::filesystem::temp_directory_path() / (std::string(name) + ".png")).string();

		feet.gfx->DrawFrame(target, job);
		feet.gfx->ScreenshotPng(target, path);

		const auto lumas = Lumas{
			bgl::test::MeanColor(path, left.x, left.y, c_Box, c_Box).Luma(),
			bgl::test::MeanColor(path, right.x, right.y, c_Box, c_Box).Luma(),
		};
		std::filesystem::remove(path);
		return lumas;
	};

	const Lumas base = sample("bernini_feet_base");
	REQUIRE(base.left > 0.05f);
	REQUIRE(base.right > 0.05f);

	// A body disc narrower than the stance, so it lands between the feet and never under them.
	auto desc      = bgl::BlobShadowDesc();
	desc.radius    = 0.1f;
	desc.intensity = 0.9f;

	SECTION("without feet the ground under each foot is untouched")
	{
		feet.view->SetBlobShadow(instance, desc);
		const Lumas body = sample("bernini_feet_body");
		CHECK(body.left > base.left * 0.98f);
		CHECK(body.right > base.right * 0.98f);
	}

	SECTION("with feet the planted foot is dark and the lifted one fades")
	{
		desc.feet            = bgl::FootShadowDesc();
		desc.feet->radius    = 0.12f;
		desc.feet->intensity = 0.9f;

		// The capture is tonemapped: 0.9 of darkening in linear light reads as about half the luma.
		desc.feet->fadeHeight = 0.3f;
		feet.view->SetBlobShadow(instance, desc);
		const Lumas shaded = sample("bernini_feet_shaded");

		CHECK(shaded.left < base.left * 0.6f);
		CHECK(shaded.right < base.right * 0.9f);
		CHECK(shaded.right > shaded.left * 1.3f);

		// Past its fade height the lifted foot takes nothing, and the planted one is unmoved.
		desc.feet->fadeHeight = c_Lift * 0.5f;
		feet.view->SetBlobShadow(instance, desc);
		const Lumas faded = sample("bernini_feet_faded");
		CHECK(faded.right > base.right * 0.97f);
		CHECK(faded.left < base.left * 0.6f);
	}

	SECTION("a zero-intensity body disc leaves the feet alone and draws nothing itself")
	{
		desc.intensity    = 0.0f;
		desc.feet         = bgl::FootShadowDesc();
		desc.feet->radius = 0.12f;
		feet.view->SetBlobShadow(instance, desc);
		const Lumas feetOnly = sample("bernini_feet_only");
		CHECK(feetOnly.left < base.left * 0.8f);
	}
}

TEST_CASE("only a hero whose rig authored legs may cast foot shadows", "[blobshadow][skinned]")
{
	const FootScene feet = MakeFootScene(0.0f, false);

	auto desc = bgl::BlobShadowDesc();
	desc.feet = bgl::FootShadowDesc();

	const auto hero = feet.view->CreateSkinnedMeshInstance(
		feet.geom,
		glm::mat4(1.0f),
		bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });

	SECTION("a hero with legs takes the record, and gets it back")
	{
		desc.feet->radius          = 0.2f;
		desc.feet->maxReceiverRise = 0.05f;
		feet.view->SetBlobShadow(hero, desc);

		const auto stored = feet.view->GetBlobShadow(hero);
		REQUIRE(stored.has_value());
		REQUIRE(stored->feet.has_value());
		CHECK(stored->feet->radius == 0.2f);
		CHECK(stored->feet->maxReceiverRise == 0.05f);
	}

	SECTION("a crowd instance, a static placement and a rig without legs are refused")
	{
		const auto crowd = feet.view->CreateSkinnedMeshInstance(
			feet.geom,
			glm::mat4(1.0f),
			bgl::SkinnedInstanceDesc{ 0, 0.0f, 1.0f, bgl::PoseSource::kBoneAnimTable });
		CHECK_THROWS_AS(feet.view->SetBlobShadow(crowd, desc), bgl::SceneError);
		CHECK_THROWS_AS(feet.view->SetBlobShadow(feet.ground, desc), bgl::SceneError);

		const bgl::RigHandle legless =
			feet.scene->AddRig(MakeTwoLegRig(), MakeStance(0.0f), bgl::FootPlantDesc());
		const std::array<bgl::MaterialHandle, 1> materials = { { feet.scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc()) } };
		const auto                               geom      = feet.scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			legless,
			assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
		const auto leglessHero = feet.view->CreateSkinnedMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::SkinnedInstanceDesc{ 0, 0.0f, 0.0f });
		CHECK_THROWS_AS(feet.view->SetBlobShadow(leglessHero, desc), bgl::SceneError);

		// The body disc alone is still anyone's.
		desc.feet.reset();
		CHECK_NOTHROW(feet.view->SetBlobShadow(crowd, desc));
		CHECK_NOTHROW(feet.view->SetBlobShadow(leglessHero, desc));
	}

	SECTION("a foot field out of bounds is refused, and leaves the record as it was")
	{
		feet.view->SetBlobShadow(hero, desc);

		const float nan    = std::numeric_limits<float>::quiet_NaN();
		const auto  refuse = [&](auto&& spoil) {
			auto bad = desc;
			spoil(*bad.feet);
			CHECK_THROWS_AS(feet.view->SetBlobShadow(hero, bad), bgl::SceneError);
		};
		refuse([](bgl::FootShadowDesc& f) { f.radius = 0.0f; });
		refuse([&](bgl::FootShadowDesc& f) { f.radius = nan; });
		refuse([](bgl::FootShadowDesc& f) { f.intensity = -0.1f; });
		refuse([](bgl::FootShadowDesc& f) { f.intensity = 1.5f; });
		refuse([](bgl::FootShadowDesc& f) { f.fadeHeight = 0.0f; });
		refuse([](bgl::FootShadowDesc& f) { f.maxReceiverRise = -1.0f; });
		refuse([&](bgl::FootShadowDesc& f) { f.maxReceiverRise = nan; });

		CHECK(feet.view->GetBlobShadow(hero)->feet->radius == desc.feet->radius);
	}
}
