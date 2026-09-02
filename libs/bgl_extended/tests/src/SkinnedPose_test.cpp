#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "gfx/GraphicsBase.h"
#include "resource/Readback.h"
#include "resource/ResourceManager.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/PaletteReadback.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include <assetlib/skinning.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// What the pose pass computes, read straight off the GPU. A golden image can tell you a skinned pose
// is wrong; only this can tell you *which* stage got it wrong -- a bad inverse bind, a hierarchy walk
// that skipped a level, and a mis-strided sample fetch all look the same on screen.

namespace
{
	constexpr uint32_t c_Width      = 256;
	constexpr uint32_t c_Height     = 256;
	constexpr uint32_t c_BoneCount  = 3;
	constexpr float    c_SampleRate = 30.0f;

	// Frame 0 is the bind pose, frame 1 swings bone 1 by 90 degrees about +Z. Two frames is the
	// minimum that makes interpolation observable, and the rig is a chain so a walk that ignored
	// `parent` cannot pass.
	constexpr uint32_t c_Frames = 2;

	/**
	 * A three-bone chain standing up +Y: bone 0 at the origin, each child one unit above its parent.
	 * Every inverse bind is the exact inverse of that bind pose, so a pose *equal* to the bind pose
	 * must produce an identity palette -- which is the cheapest possible check that the inverse binds
	 * and the walk agree.
	 */
	assetlib::Skeleton
	MakeChain(uint32_t boneCount = c_BoneCount)
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < boneCount; ++i)
		{
			auto bone     = assetlib::Bone();
			bone.bindPose = { glm::vec3(0.0f, i == 0 ? 0.0f : 1.0f, 0.0f),
				              glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				              glm::vec3(1.0f) };

			// Model-space bind of bone i is (0, i, 0), so its inverse takes a model point back by it.
			bone.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -float(i), 0.0f));
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = 0;
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	assetlib::AnimationSet
	MakeSwingClip(uint32_t boneCount = c_BoneCount)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = boneCount;

		// Rz(90) as xyzw = (0, 0, sin45, cos45); glm::quat takes w first.
		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

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
		clip.loop        = 0;  // one-shot, so a held frame is exactly the frame asked for
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/**
	 * The swing set with a second one-shot clip after it: frame 1 swings bone 2 by 90 degrees about
	 * +X and slides the root by half a unit along +X, so a blend of the two clips moves a bone the
	 * other never touches and a translation the other never has.
	 */
	assetlib::AnimationSet
	MakeTwoClipSet()
	{
		auto set = MakeSwingClip();

		// The CPU reference checks the pair; the upload does not.
		set.skeletonSignature = assetlib::skeletonSignature(MakeChain());

		// Rx(90) as xyzw = (sin45, 0, 0, cos45); glm::quat takes w first.
		const auto tilt = glm::quat(0.70710678f, 0.70710678f, 0.0f, 0.0f);

		const uint32_t firstSample = static_cast<uint32_t>(set.samples.size());
		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_BoneCount; ++b)
			{
				auto sample        = assetlib::Transform();
				sample.translation = glm::vec3(0.0f, b == 0 ? 0.0f : 1.0f, 0.0f);
				if (f == 1 && b == 0)
				{
					sample.translation.x = 0.5f;
				}
				sample.rotation = (f == 1 && b == 2) ? tilt : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale    = glm::vec3(1.0f);
				set.samples.push_back(sample);
			}
		}

		auto clip        = assetlib::AnimationClip();
		clip.firstSample = firstSample;
		clip.frameCount  = c_Frames;
		clip.sampleRate  = c_SampleRate;
		clip.duration    = 1.0f / c_SampleRate;
		clip.loop        = 0;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/**
	 * The swing again, but authored as a loop: three frames whose last repeats the first, which is
	 * both what the importer produces and what makes it a loop (`posesMatch(first, last)`). A cycle
	 * is therefore two intervals, not three -- see clip_playback.slang.
	 */
	assetlib::AnimationSet
	MakeSwingLoop()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_BoneCount;

		const auto swing = glm::quat(0.70710678f, 0.0f, 0.0f, 0.70710678f);

		for (uint32_t f = 0; f < 3; ++f)
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
		clip.frameCount  = 3;
		clip.sampleRate  = c_SampleRate;
		clip.duration    = 2.0f / c_SampleRate;
		clip.loop        = 1;
		clip.nameOffset  = 0;
		set.clips.push_back(clip);

		return set;
	}

	/** One triangle carrying skin binding; the geometry is irrelevant, the rig is the subject. */
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

	/**
	 * The palette against assetlib's weighted reference, through the points a skinning matrix moves
	 * rather than the matrix itself: the reference is column-major glm and the palette holds three
	 * rows as the shader indexed them, so what both agree on is where a point lands.
	 */
	void
	CheckAgainstReference(
		const bgl::test::Palette&              palette,
		const assetlib::Skeleton&              skeleton,
		const assetlib::AnimationSet&          animations,
		std::span<const assetlib::BlendSample> blend)
	{
		const std::vector<glm::mat4> expected = assetlib::skinningMatrices(
			skeleton,
			assetlib::poseModelTransforms(skeleton, animations, blend));

		const std::array<glm::vec3, 3> probes = {
			{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f }, { 0.5f, -0.25f, 2.0f } }
		};

		for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
		{
			INFO("bone " << bone);
			for (const glm::vec3& probe : probes)
			{
				bgl::test::CheckNear(
					palette.Apply(bone, probe),
					glm::vec3(expected[bone] * glm::vec4(probe, 1.0f)));
			}
		}
	}

	/** A slot playing `node` held at `phase`, whose weight ramps `from` to `to` over the window. */
	bgl::PlaybackSlot
	RampedSlot(uint32_t node, float phase, float from, float to, float rampStart, float rampEnd)
	{
		auto slot      = bgl::PlaybackSlot();
		slot.node      = node;
		slot.phase     = phase;
		slot.rate      = 0.0f;
		slot.weight0   = from;
		slot.weight1   = to;
		slot.rampStart = rampStart;
		slot.rampEnd   = rampEnd;
		return slot;
	}
}

// The blend gates: what the pose pass writes for a record of several weighted slots, held against
// the CPU reference the same slots evaluate to. The reference is what defines a blend here; the
// palette has to agree with it, not with a hand-derived matrix.
TEST_CASE("the pose pass blends a record's slots as the reference does", "[skinned][pose][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

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

	const assetlib::Skeleton     skeleton   = MakeChain();
	const assetlib::AnimationSet animations = MakeTwoClipSet();

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedTriangle(),
		0,
		materials,
		scene->AddRig(skeleton, animations),
		assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
	REQUIRE(geom.IsValid());

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

	auto* viewRaw = view->As<bgl::SceneView>();
	REQUIRE(viewRaw != nullptr);

	const uint32_t float4sPerPose = bgl::idl::cFloat4sPerBone * c_BoneCount;

	// Clip 0 fading into clip 1 over t in [0.5, 1.5], both held at frame 1, where each differs from
	// the bind pose.
	auto crossfade    = bgl::SkinnedPlaybackDesc();
	crossfade.slot[0] = RampedSlot(0, 1.0f, 1.0f, 0.0f, 0.5f, 1.5f);
	crossfade.slot[1] = RampedSlot(1, 1.0f, 0.0f, 1.0f, 0.5f, 1.5f);

	const auto paletteAt = [&](bgl::MeshInstanceHandle instance, float time) {
		job.time = time;
		gfx->DrawFrame(target, job);
		return bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);
	};

	SECTION("two clips mid-crossfade")
	{
		const auto instance = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), crossfade);

		// Halfway along the ramp the weights are equal; a quarter of the way they are 3:1. Both are
		// checked, because equal weights hide a slot read in the wrong order.
		const std::array<assetlib::BlendSample, 2> even = { { { 0, 1.0f, 0.5f },
			                                                  { 1, 1.0f, 0.5f } } };
		CheckAgainstReference(paletteAt(instance, 1.0f), skeleton, animations, even);

		const std::array<assetlib::BlendSample, 2> lopsided = { { { 0, 1.0f, 0.75f },
			                                                      { 1, 1.0f, 0.25f } } };
		CheckAgainstReference(paletteAt(instance, 0.75f), skeleton, animations, lopsided);
	}

	SECTION("a weight ramp read before and after its window is one clip alone")
	{
		const auto instance = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), crossfade);

		const std::array<assetlib::BlendSample, 1> from = { { { 0, 1.0f, 1.0f } } };
		CheckAgainstReference(paletteAt(instance, 0.25f), skeleton, animations, from);

		const std::array<assetlib::BlendSample, 1> to = { { { 1, 1.0f, 1.0f } } };
		CheckAgainstReference(paletteAt(instance, 2.0f), skeleton, animations, to);
	}

	SECTION("a rewrite rebased to now leaves the prevTime palette what the old record gave")
	{
		// Clip 0 at rate 1 from the clock's zero: at t1 it has reached frame 1 and holds there.
		const auto instance = view->CreateSkinnedMeshInstance(
			geom,
			glm::mat4(1.0f),
			bgl::SkinnedInstanceDesc{ 0, 0.0f, 1.0f });

		const float t1 = 1.0f / c_SampleRate;
		paletteAt(instance, 0.0f);
		paletteAt(instance, t1);

		// At t1 something happens: a crossfade to clip 1 over half a second. Both slots are rebased
		// to t1 -- slot 0's phase re-expressed there, its ramp starting there -- so the record
		// evaluated at t1 is still clip 0 at frame 1 at full weight.
		auto rewrite         = bgl::SkinnedPlaybackDesc();
		rewrite.slot[0]      = RampedSlot(0, 1.0f, 1.0f, 0.0f, t1, t1 + 0.5f);
		rewrite.slot[0].rate = 1.0f;
		rewrite.slot[0].tRef = t1;
		rewrite.slot[1]      = RampedSlot(1, 0.0f, 0.0f, 1.0f, t1, t1 + 0.5f);
		rewrite.slot[1].tRef = t1;
		view->SetSkinnedPlayback(instance, rewrite);

		// Half a frame later. prevTime is t1, and the second palette must be the pose the old
		// record drew there -- which is what the motion vector reprojects through.
		const float t2 = t1 + 0.5f / c_SampleRate;
		job.time       = t2;
		gfx->DrawFrame(target, job);

		const bgl::test::Palette both = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose * 2);

		auto previous = bgl::test::Palette();
		previous.rows.assign(both.rows.begin() + float4sPerPose, both.rows.end());
		const std::array<assetlib::BlendSample, 1> old = { { { 0, 1.0f, 1.0f } } };
		CheckAgainstReference(previous, skeleton, animations, old);

		// And the current half is the fade begun: 1/30 of the way along, clip 0 still clamped at
		// its last frame and clip 1 at its first.
		auto current = bgl::test::Palette();
		current.rows.assign(both.rows.begin(), both.rows.begin() + float4sPerPose);
		const float                                fade = (0.5f / c_SampleRate) / 0.5f;
		const std::array<assetlib::BlendSample, 2> now  = { { { 0, 1.0f, 1.0f - fade },
			                                                  { 1, 0.0f, fade } } };
		CheckAgainstReference(current, skeleton, animations, now);
	}
}

// Where the render suite proves a deep rig draws right, this proves every bone of one is right --
// including the ones no vertex weights, which a picture cannot see. 192 was the old ceiling, set by
// the groupshared array the walk used to hold; the walk now composes in the palette, so the only
// thing a bone count costs is palette.
TEST_CASE("a rig far past the old ceiling poses every bone", "[skinned][pose][render]")
{
	constexpr uint32_t c_DeepBones = 300;

	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = dynamic_cast<bgl::GraphicsBase*>(gfx.Get());
	REQUIRE(gfxBase != nullptr);

	auto targetDesc     = bgl::RenderTargetDesc();
	targetDesc.width    = 64;
	targetDesc.height   = 64;
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

	// Tilted and lowered: a rig with no legs must pose identically on any ground.
	scene->SetGround({ glm::vec3(0.0f, -0.3f, 0.0f), glm::vec3(0.2f, 1.0f, 0.1f) });

	auto view = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.baseColorFactor = glm::vec4(1.0f);
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedTriangle(),
		0,
		materials,
		scene->AddRig(MakeChain(c_DeepBones), MakeSwingClip(c_DeepBones)),
		assetlib::Bounds{ glm::vec3(-400.0f), glm::vec3(400.0f) });
	REQUIRE(geom.IsValid());

	auto* viewRaw = dynamic_cast<bgl::SceneView*>(view.Get());
	REQUIRE(viewRaw != nullptr);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.viewport = bgl::Viewport(64.0f, 64.0f);

	const uint32_t float4sPerPose = bgl::idl::cFloat4sPerBone * c_DeepBones;

	SECTION("at bind pose every one of the 300 is identity")
	{
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		for (uint32_t bone = 0; bone < c_DeepBones; ++bone)
			bgl::test::CheckNear(
				palette.Apply(bone, glm::vec3(1.0f, 2.0f, 3.0f)),
				glm::vec3(1.0f, 2.0f, 3.0f));
	}

	SECTION("swung, the root's rotation still reaches the 299th bone")
	{
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		// Bone 0 is the root and never moves.
		bgl::test::CheckNear(palette.Apply(0, glm::vec3(0.0f)), glm::vec3(0.0f));

		// Every bone below the swing lands its own origin on (1,1,0), whatever its depth: the chain
		// continues along the rotated axis, and each bone's inverse bind takes exactly its own bind
		// offset back off again. A bone that never composed through its parent would report
		// (0, 1 - i, 0) instead -- at bone 299 that is 298 units away, not a rounding difference.
		for (uint32_t bone = 1; bone < c_DeepBones; ++bone)
		{
			INFO("bone " << bone);
			bgl::test::CheckNear(palette.Apply(bone, glm::vec3(0.0f)), glm::vec3(1.0f, 1.0f, 0.0f));
		}
	}
}

TEST_CASE("the pose pass writes the palette a rig's hierarchy implies", "[skinned][pose][render]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;

	auto gfx = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto* gfxBase = gfx->As<bgl::GraphicsBase>();
	REQUIRE(gfxBase != nullptr);

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

	// Tilted and lowered: a rig with no legs must pose identically on any ground.
	scene->SetGround({ glm::vec3(0.0f, -0.3f, 0.0f), glm::vec3(0.2f, 1.0f, 0.1f) });

	auto view = gfx->CreateSceneView(scene, 4);

	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;
	const auto pbr           = scene->CreatePbrMaterial(material);

	const std::array<bgl::MaterialHandle, 1> materials = { { pbr } };

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedTriangle(),
		0,
		materials,
		scene->AddRig(MakeChain(), MakeSwingClip()),
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

	const uint32_t float4sPerPose = bgl::idl::cFloat4sPerBone * c_BoneCount;

	SECTION("a bind-pose frame gives an identity palette")
	{
		// rate 0 holds frame 0, which is the bind pose: pose * inverseBind is then exactly identity
		// for every bone. A wrong inverse bind, a missed hierarchy level and a mis-strided sample
		// fetch all break this, which is why it is the first thing asserted.
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
		{
			bgl::test::CheckNear(
				palette.Apply(bone, glm::vec3(0.0f, float(bone), 0.0f)),
				glm::vec3(0.0f, float(bone), 0.0f));
			bgl::test::CheckNear(
				palette.Apply(bone, glm::vec3(1.0f, 2.0f, 3.0f)),
				glm::vec3(1.0f, 2.0f, 3.0f));
		}
	}

	SECTION("a parent's rotation reaches its grandchild")
	{
		// Frame 1 swings bone 1 by 90 degrees about +Z, about its own bind position (0,1,0).
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		// Bone 0 never moves: it is the root and its own local pose is unchanged.
		bgl::test::CheckNear(palette.Apply(0, glm::vec3(0.0f, 0.0f, 0.0f)), glm::vec3(0.0f));

		// Bone 1 rotates about its own origin, so its bind position is a fixed point -- and a point
		// one unit above it swings onto -X.
		bgl::test::CheckNear(
			palette.Apply(1, glm::vec3(0.0f, 1.0f, 0.0f)),
			glm::vec3(0.0f, 1.0f, 0.0f));
		bgl::test::CheckNear(
			palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(-1.0f, 1.0f, 0.0f));

		// Bone 2 has no local rotation of its own: everything it does here it inherited through the
		// walk. Its own base sits one unit above bone 1's pivot and its tip two, so the swing takes
		// them onto -X at those distances -- which is also the check that the walk composed exactly
		// once. Composing twice would double the offset; skipping the level would leave both at +Y.
		bgl::test::CheckNear(
			palette.Apply(2, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(-1.0f, 1.0f, 0.0f));
		bgl::test::CheckNear(
			palette.Apply(2, glm::vec3(0.0f, 3.0f, 0.0f)),
			glm::vec3(-2.0f, 1.0f, 0.0f));
	}

	SECTION("two placements of one geom pose from their own records")
	{
		// The pass's work list names mesh instances, so each workgroup reaches its playback record
		// through the placement it was given. A list naming the wrong one, or a lookup that lost
		// the pairing, poses both instances from whichever record it landed on -- and the two here
		// are asked for different frames of the same clip, so that swap is visible.
		//
		// They stand in different places, which must not reach the palette at all: a bone matrix is
		// model space, and where the instance stands is applied downstream in the mesh shader.
		const auto held = view->CreateSkinnedMeshInstance(
			geom,
			glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)),
			{ 0, 0.0f, 0.0f });
		const auto swung = view->CreateSkinnedMeshInstance(
			geom,
			glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f)),
			{ 0, 1.0f, 0.0f });

		gfx->DrawFrame(target, job);

		const bgl::test::Palette heldPalette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, held),
			float4sPerPose);
		const bgl::test::Palette swungPalette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, swung),
			float4sPerPose);

		for (uint32_t bone = 0; bone < c_BoneCount; ++bone)
		{
			bgl::test::CheckNear(
				heldPalette.Apply(bone, glm::vec3(1.0f, 2.0f, 3.0f)),
				glm::vec3(1.0f, 2.0f, 3.0f));
		}

		bgl::test::CheckNear(
			swungPalette.Apply(2, glm::vec3(0.0f, 3.0f, 0.0f)),
			glm::vec3(-2.0f, 1.0f, 0.0f));
	}

	SECTION("a fractional frame blends the two it falls between")
	{
		// Half of a 90-degree swing is 45, and nlerp of the two endpoint quaternions is exactly the
		// half-angle rotation here (a single axis, so the shortest arc is unambiguous).
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.5f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		const float c = std::cos(glm::radians(45.0f));
		const float s = std::sin(glm::radians(45.0f));

		// (0,1,0) relative to bone 1's origin, rotated 45 degrees about +Z.
		bgl::test::CheckNear(
			palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(-s, 1.0f + c, 0.0f));
	}

	SECTION("growing the arena leaves every live instance's palette intact")
	{
		// A 3-bone slice is 18 float4s and the arena starts at 256, so the fifteenth instance forces a
		// Resize -- which *discards* the arena's contents. That is only safe because the pass reposes
		// every live instance every frame rather than the ones that changed, and this is what pins it:
		// after the growth, the first instance placed must still read back correctly.
		constexpr uint32_t c_Instances = 20;

		auto handles = std::vector<bgl::MeshInstanceHandle>();
		for (uint32_t i = 0; i < c_Instances; ++i)
		{
			handles.push_back(
				view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 1.0f, 0.0f }));
			REQUIRE(handles.back().IsValid());
		}

		REQUIRE(viewRaw->GetPalettes().Capacity() > 256);

		gfx->DrawFrame(target, job);
		CHECK(viewRaw->GetPosedInstanceCount() == c_Instances);

		// The first and the last, so both a slice allocated before the growth and one allocated after
		// it are covered.
		for (const uint32_t which : { 0u, c_Instances - 1 })
		{
			const bgl::test::Palette palette = bgl::test::ReadPalette(
				gfxBase,
				viewRaw,
				bgl::test::PaletteBaseOf(viewRaw, handles[which]),
				float4sPerPose);
			bgl::test::CheckNear(
				palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
				glm::vec3(-1.0f, 1.0f, 0.0f));
			bgl::test::CheckNear(
				palette.Apply(2, glm::vec3(0.0f, 3.0f, 0.0f)),
				glm::vec3(-2.0f, 1.0f, 0.0f));
		}
	}

	SECTION("a loop's cycle is frameCount - 1 frames")
	{
		// The same swing authored as a loop: [bind, swung, bind]. A cycle is two intervals, so
		// phase 2.5 is half an interval into the second cycle -- the half-swing, exactly what
		// phase 0.5 gives. Wrapping over frameCount instead would spend a third interval blending
		// frame 2 onto frame 0, which are the same pose, and leave this at the bind pose.
		const auto looping = scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			scene->AddRig(MakeChain(), MakeSwingLoop()),
			assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
		REQUIRE(looping.IsValid());

		const auto instance =
			view->CreateSkinnedMeshInstance(looping, glm::mat4(1.0f), { 0, 2.5f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		const float c = std::cos(glm::radians(45.0f));
		const float s = std::sin(glm::radians(45.0f));

		bgl::test::CheckNear(
			palette.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(-s, 1.0f + c, 0.0f));
	}

	SECTION("a whole cycle returns a loop to its first frame")
	{
		// Phase 2.0 is exactly one cycle, so the pose is frame 0's -- the bind pose, which this rig
		// answers with an identity palette.
		const auto looping = scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			scene->AddRig(MakeChain(), MakeSwingLoop()),
			assetlib::Bounds{ glm::vec3(-4.0f), glm::vec3(4.0f) });
		REQUIRE(looping.IsValid());

		const auto instance =
			view->CreateSkinnedMeshInstance(looping, glm::mat4(1.0f), { 0, 2.0f, 0.0f });
		gfx->DrawFrame(target, job);

		const bgl::test::Palette palette = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose);

		for (uint32_t b = 0; b < c_BoneCount; ++b)
		{
			bgl::test::CheckNear(
				palette.Apply(b, glm::vec3(1.0f, 2.0f, 3.0f)),
				glm::vec3(1.0f, 2.0f, 3.0f));
		}
	}

	SECTION("the prevTime palette sits a whole pose after the current one")
	{
		// rate 1 from phase 0: at time = one frame the current pose is frame 1 and the previous, one
		// frame of clock earlier, is frame 0 -- the bind pose. That pair is what a motion vector is
		// derived from, so the two halves must not be the same bytes.
		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 1.0f });

		// Two frames, because prevTime equals time on the first one by construction (see ViewData) --
		// a single draw would compare a pose against itself and pass on a palette that never wrote
		// its second half.
		job.time = 0.0f;
		gfx->DrawFrame(target, job);

		job.time = 1.0f / c_SampleRate;
		gfx->DrawFrame(target, job);

		const bgl::test::Palette both = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			float4sPerPose * 2);

		// Current half: bone 1 swung.
		bgl::test::CheckNear(
			both.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(-1.0f, 1.0f, 0.0f));

		// Previous half, one pose further in: still the bind pose, so identity.
		auto previous = bgl::test::Palette();
		previous.rows.assign(both.rows.begin() + float4sPerPose, both.rows.end());
		bgl::test::CheckNear(
			previous.Apply(1, glm::vec3(0.0f, 2.0f, 0.0f)),
			glm::vec3(0.0f, 2.0f, 0.0f));
	}
}
