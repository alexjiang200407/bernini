#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/GoldenImage.h"
#include "util/PaletteReadback.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <algorithm>
#include <array>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Node.h>
#include <assetlib_structs/Skeleton.h>
#include <assetlib_structs/VertexLayout.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <bgl/MaterialHandle.h>
#include <bgl/RigHandle.h>
#include <bgl/types/FootIKDesc.h>
#include <bgl/types/FootPlantDesc.h>
#include <bgl/types/GroundPlaneDesc.h>
#include <bgl_common/idl/Constants.h>
#include <bgl_common/idl/FootIKLeg.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <vector>

// What the foot-plant solve writes into the palette, read straight off the GPU. A golden image can
// say a foot is in the wrong place; only this can say whether the two-bone solve, the sole tilt or
// the descendant fixup is what put it there.
//
// The rig is a leg hanging off a pelvis, posed at its own bind pose by a clip that never moves. So
// every palette entry would be identity if the solve did nothing, and anything not identity here is
// the solve and nothing else.

namespace
{
	constexpr uint32_t c_Pelvis = 0;
	constexpr uint32_t c_Hip    = 1;
	constexpr uint32_t c_Knee   = 2;
	constexpr uint32_t c_Ankle  = 3;
	constexpr uint32_t c_Toe    = 4;
	constexpr uint32_t c_Bones  = 5;

	constexpr uint32_t c_Frames = 2;

	// The ankle sits this far above its sole: the sole plane is `c_SolePoint` in ankle-local space
	// with `c_SoleNormal` up, and the ankle's bind is the model origin.
	constexpr float c_FootHeight = 0.1f;

	const auto c_SolePoint  = glm::vec3(0.0f, -c_FootHeight, 0.0f);
	const auto c_SoleNormal = glm::vec3(0.0f, 1.0f, 0.0f);

	// Model-space bind positions. The knee is carried forward in +X so the leg has a bend plane at
	// all -- a chain that is already straight has no plane to bend in, and the solve says so by
	// leaving it alone -- and far enough forward that the leg has travel: hip to ankle is 2 against
	// a 2.33 reach, so it can extend a third of a unit before it runs out and wants the pelvis drop
	// that is a later stage.
	//
	// Lifted by c_FootHeight so the sole rests on y = 0, which is where a clip that has been
	// through `groundClips` stands: the plant applies the ground's departure from that floor, so a
	// fixture hanging below it would be measuring a clip the cook never produces.
	const std::array<glm::vec3, c_Bones> c_Bind = { {
		glm::vec3(0.0f, 2.0f + c_FootHeight, 0.0f),  // pelvis
		glm::vec3(0.0f, 2.0f + c_FootHeight, 0.0f),  // hip
		glm::vec3(0.6f, 1.0f + c_FootHeight, 0.0f),  // knee
		glm::vec3(0.0f, 0.0f + c_FootHeight, 0.0f),  // ankle
		glm::vec3(0.2f, 0.0f + c_FootHeight, 0.0f),  // toe
	} };

	/**
	 * A bone's local TRS with the rig's root scaled by `rootScale`: the root carries the scale and
	 * every child is authored in the units that scale produces, which is how a rig exported in
	 * centimetres arrives -- a 0.01 on the root and a hundredfold in every inverse bind below it.
	 */
	assetlib::Transform
	LocalOf(uint32_t bone, float rootScale)
	{
		const glm::vec3 parent = bone == 0 ? glm::vec3(0.0f) : c_Bind[bone - 1];
		return { (c_Bind[bone] - parent) / (bone == 0 ? 1.0f : rootScale),
			     glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
			     glm::vec3(bone == 0 ? rootScale : 1.0f) };
	}

	assetlib::Skeleton
	MakeLegRig(float rootScale = 1.0f)
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_Bones; ++i)
		{
			auto bone        = assetlib::Bone();
			bone.bindPose    = LocalOf(i, rootScale);
			bone.inverseBind = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / rootScale)) *
			                   glm::translate(glm::mat4(1.0f), -c_Bind[i]);
			bone.parent      = i == 0 ? assetlib::c_InvalidIndex : i - 1;
			bone.nameOffset  = skeleton.stringPool.add(std::format("Bone{}", i));
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/**
	 * Frame 0 is the bind pose itself, so at rate 0 nothing but the solve can move a bone. Frame 1
	 * swings the hip, which only a case that runs the clock ever reaches.
	 */
	assetlib::AnimationSet
	MakeStillClip(float rootScale = 1.0f)
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_Bones;

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_Bones; ++b)
			{
				// Rz(25) as w-first, on the hip of frame 1 alone.
				const auto swung = glm::quat(0.97437f, 0.0f, 0.0f, 0.22495f);

				assetlib::Transform sample = LocalOf(b, rootScale);
				if (f == 1 && b == c_Hip)
					sample.rotation = swung;
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

	/** What a case varies beyond the ground and the weight. */
	struct PoseOptions
	{
		glm::mat4 world      = glm::mat4(1.0f);
		glm::vec3 solePoint  = c_SolePoint;  // the sole's offset from the ankle, model units
		glm::vec3 soleNormal = c_SoleNormal;
		float     rootScale  = 1.0f;
		bool      plantFeet  = true;
	};

	bgl::FootPlantDesc
	MakeLeg(uint8_t weight, const PoseOptions& options = {})
	{
		auto leg       = bgl::FootPlantLegDesc();
		leg.hip        = c_Hip;
		leg.knee       = c_Knee;
		leg.ankle      = c_Ankle;
		leg.toe        = c_Toe;
		leg.solePoint  = options.solePoint / options.rootScale;
		leg.soleNormal = options.soleNormal;

		auto plant         = bgl::FootPlantDesc();
		plant.legs         = { leg };
		plant.plantWeights = std::vector<uint8_t>(c_Frames, weight);
		return plant;
	}
}

namespace
{
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
	 * One posed instance of the leg rig, read back. `world` places the instance, so a case can prove
	 * the ground is sampled in world space rather than in the rig's own.
	 */
	struct Posed
	{
		bgl::test::Palette palette;

		[[nodiscard]] glm::vec3
		Bone(uint32_t bone) const
		{
			return palette.Apply(bone, c_Bind[bone]);
		}

		/** Where the sole's contact point ends up, in the rig's model space. */
		glm::vec3 solePoint  = c_SolePoint;
		glm::vec3 soleNormal = c_SoleNormal;

		[[nodiscard]] glm::vec3
		Sole() const
		{
			return palette.Apply(c_Ankle, c_Bind[c_Ankle] + solePoint);
		}

		/** The sole's normal after posing, in the rig's model space. */
		[[nodiscard]] glm::vec3
		SoleNormal() const
		{
			return glm::normalize(
				palette.Apply(c_Ankle, c_Bind[c_Ankle] + solePoint + soleNormal) - Sole());
		}
	};

	/** A device, a scene standing on `ground`, and a view, with the leg rig added as one geom. */
	struct LegScene
	{
		bgl::GraphicsRef  gfx;
		bgl::SceneRef     scene;
		bgl::SceneViewRef view;
		bgl::GeomHandle   geom;
	};

	LegScene
	MakeLegScene(
		const bgl::GroundPlaneDesc& ground,
		uint8_t                     weight,
		const PoseOptions&          options = {})
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto legScene = LegScene();
		legScene.gfx  = bgl::CreateGraphics(opts);
		REQUIRE(legScene.gfx != nullptr);

		auto sceneDesc                        = bgl::SceneDesc();
		sceneDesc.initialGeom                 = 4;
		sceneDesc.initialMeshlets             = 8;
		sceneDesc.initialSubmeshes            = 4;
		sceneDesc.initialVertexBufferByteSize = 4096;
		sceneDesc.initialIndices              = 64;
		sceneDesc.initialPbrMaterials         = 4;

		legScene.scene = legScene.gfx->CreateScene(sceneDesc);
		legScene.scene->SetGround(ground);
		legScene.scene->SetFootPlanting(options.plantFeet);

		legScene.view = legScene.gfx->CreateSceneView(legScene.scene, 4);
		bgl::test::ApplyEnvironment(legScene.scene.Get(), legScene.view.Get());

		const std::array<bgl::MaterialHandle, 1> materials = { { legScene.scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc()) } };

		const bgl::RigHandle rig = legScene.scene->AddRig(
			MakeLegRig(options.rootScale),
			MakeStillClip(options.rootScale),
			MakeLeg(weight, options));
		REQUIRE(rig.IsValid());

		legScene.geom = legScene.scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			rig,
			assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) });
		REQUIRE(legScene.geom.IsValid());
		return legScene;
	}

	Posed
	PoseLeg(const bgl::GroundPlaneDesc& ground, uint8_t weight, const PoseOptions& options = {})
	{
		const LegScene legScene = MakeLegScene(ground, weight, options);
		auto&          gfx      = legScene.gfx;
		auto&          view     = legScene.view;

		auto* gfxBase = gfx->As<bgl::GraphicsBase>();
		REQUIRE(gfxBase != nullptr);

		auto targetDesc     = bgl::RenderTargetDesc();
		targetDesc.width    = 64;
		targetDesc.height   = 64;
		targetDesc.headless = true;
		auto target         = gfx->CreateRenderTarget(targetDesc);

		auto* viewRaw = view->As<bgl::SceneView>();
		REQUIRE(viewRaw != nullptr);

		// rate 0, so the clip holds frame 0 and the clock cannot move the pose between the two
		// palettes -- the solve is then the only thing that differs from the bind pose.
		const auto instance =
			view->CreateSkinnedMeshInstance(legScene.geom, options.world, { 0, 0.0f, 0.0f });

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = bgl::Viewport(64.0f, 64.0f);
		gfx->DrawFrame(target, job);

		auto posed       = Posed();
		posed.solePoint  = options.solePoint;
		posed.soleNormal = options.soleNormal;
		posed.palette    = bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			2 * bgl::idl::cFloat4sPerBone * c_Bones);
		return posed;
	}

	const auto c_Flat = bgl::GroundPlaneDesc{ glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };
}

TEST_CASE("a planted foot meets the ground under it", "[skinned][pose][plant][render]")
{
	SECTION("a foot already standing on the floor is not moved")
	{
		const Posed posed = PoseLeg(c_Flat, 255);

		// The rig already stands on y = 0, which is where groundClips puts a clip's lowest sole, so
		// the contact is on the ground before the solve runs and the solve has nothing to do. This
		// is what a plant on a *grounded* clip costs: nothing. It used to cost a small leg stretch,
		// because the floor was measured at the lowest vertex and the sole sits a band above it.
		for (uint32_t bone = 0; bone < c_Bones; ++bone)
		{
			INFO("bone " << bone);
			bgl::test::CheckNear(posed.Bone(bone), c_Bind[bone]);
		}

		bgl::test::CheckNear(posed.Sole(), glm::vec3(0.0f, 0.0f, 0.0f));
		CHECK(posed.Bone(c_Ankle).y == Catch::Approx(c_FootHeight).margin(1e-4));

		// Nothing above the hip moves: lowering the rig when a leg cannot reach is a separate
		// stage, and this leg reaches.
		bgl::test::CheckNear(posed.Bone(c_Pelvis), c_Bind[c_Pelvis]);
		bgl::test::CheckNear(posed.Bone(c_Hip), c_Bind[c_Hip]);
	}

	SECTION("ground below the foot lowers it the same way")
	{
		// Within the leg's reach: a plane it cannot stretch to is the pelvis-drop case, which this
		// stage deliberately leaves alone.
		const auto low =
			bgl::GroundPlaneDesc{ glm::vec3(0.0f, -0.2f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };
		const Posed posed = PoseLeg(low, 255);

		bgl::test::CheckNear(posed.Sole(), glm::vec3(0.0f, -0.2f, 0.0f));
		bgl::test::CheckNear(posed.Bone(c_Hip), c_Bind[c_Hip]);
	}

	SECTION("a weight of zero leaves every bone at its bind pose")
	{
		const Posed posed = PoseLeg(c_Flat, 0);

		for (uint32_t bone = 0; bone < c_Bones; ++bone)
		{
			INFO("bone " << bone);
			bgl::test::CheckNear(posed.Bone(bone), c_Bind[bone]);
		}
	}

	SECTION("half a weight moves the foot half way")
	{
		// Ground the rig has to reach down to: on flat ground at the authored floor the plant is an
		// identity, so halving it would compare nothing with nothing.
		const auto low =
			bgl::GroundPlaneDesc{ glm::vec3(0.0f, -0.2f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };

		const Posed full = PoseLeg(low, 255);
		const Posed half = PoseLeg(low, 128);

		// 128/255 of the way, which is what a weight interpolates -- not a threshold.
		const float fraction = 128.0f / 255.0f;
		const float bind     = c_Bind[c_Ankle].y;
		CHECK(
			half.Bone(c_Ankle).y ==
			Catch::Approx(bind + fraction * (full.Bone(c_Ankle).y - bind)).margin(2e-3));
	}
}

// The Coyote's root is scaled 0.01 -- the rig is authored in centimetres -- so its ankle-local sole
// sits twenty bone units from the joint. Read as metres, that put the target seven metres up and
// swung the leg over the rig's head.
TEST_CASE("a rig authored in centimetres plants the same foot", "[skinned][pose][plant][render]")
{
	const Posed metres      = PoseLeg(c_Flat, 255);
	const Posed centimetres = PoseLeg(c_Flat, 255, { .rootScale = 0.01f });

	bgl::test::CheckNear(centimetres.Sole(), glm::vec3(0.0f));
	CHECK(centimetres.Bone(c_Ankle).y == Catch::Approx(c_FootHeight).margin(1e-4));

	for (uint32_t bone = 0; bone < c_Bones; ++bone)
	{
		INFO("bone " << bone);
		bgl::test::CheckNear(centimetres.Bone(bone), metres.Bone(bone));
	}
}

TEST_CASE("a planted foot turns onto the slope it stands on", "[skinned][pose][plant][render]")
{
	SECTION("a flat sole comes onto the ground normal, and stays on the plane")
	{
		const float radians = glm::radians(15.0f);
		const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
		const auto  slope   = bgl::GroundPlaneDesc{ glm::vec3(0.0f), normal };

		const Posed posed = PoseLeg(slope, 255);

		bgl::test::CheckNear(posed.SoleNormal(), normal);

		// On the plane, which for a slope through the origin means perpendicular to its normal.
		CHECK(glm::dot(posed.Sole(), normal) == Catch::Approx(0.0f).margin(1e-4));
	}

	SECTION("a foot posed on its toe keeps its heel up: the turn is the slope's, not the sole's")
	{
		// The Coyote's Success stands one foot heel-up 28 degrees. A tilt that brought the sole
		// onto the ground forced it flat on level ground, where the plant should change nothing.
		const float heel = glm::radians(20.0f);
		const auto  toe =
			PoseOptions{ .soleNormal = glm::vec3(-std::sin(heel), std::cos(heel), 0.0f) };

		const Posed level = PoseLeg(c_Flat, 255, toe);
		bgl::test::CheckNear(level.SoleNormal(), toe.soleNormal);

		// It lands on its lowest point -- the sole plane under the heel or under the toe -- and not
		// on the plane's centre, which would put the low end of the foot through the floor.
		const auto onSole = [&level](const glm::vec3& joint) {
			return joint - glm::dot(joint - level.Sole(), level.SoleNormal()) * level.SoleNormal();
		};
		const float heelY = onSole(level.Bone(c_Ankle)).y;
		const float ballY = onSole(level.Bone(c_Toe)).y;
		CHECK(std::min(heelY, ballY) == Catch::Approx(0.0f).margin(1e-4));
		CHECK(std::max(heelY, ballY) > 0.01f);

		// On a slope the heel comes up by the slope on top of what the animator gave it.
		const float radians = glm::radians(15.0f);
		const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
		const auto  slope   = bgl::GroundPlaneDesc{ glm::vec3(0.0f), normal };

		const Posed sloped = PoseLeg(slope, 255, toe);
		const float turned =
			std::acos(std::clamp(glm::dot(sloped.SoleNormal(), level.SoleNormal()), -1.0f, 1.0f));
		CHECK(turned == Catch::Approx(radians).margin(1e-3));
	}

	SECTION("past thirty degrees the ankle stops turning")
	{
		// The sole point sits on the ankle joint and the plane passes through it -- through the
		// ankle's own bind, since a grounded rig stands its sole on the floor rather than its joint
		// -- so the position target is the joint itself and the chain does not move at all. What is
		// left is the tilt alone, which is the only way to read the clamp off a palette: anywhere
		// else the shin's own rotation is folded into the same number.
		const float radians = glm::radians(45.0f);
		const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
		const auto  slope   = bgl::GroundPlaneDesc{ c_Bind[c_Ankle], normal };

		const Posed posed = PoseLeg(slope, 255, { .solePoint = glm::vec3(0.0f) });
		bgl::test::CheckNear(posed.Bone(c_Ankle), c_Bind[c_Ankle]);

		const float turned =
			std::acos(std::clamp(glm::dot(posed.SoleNormal(), c_SoleNormal), -1.0f, 1.0f));
		CHECK(turned == Catch::Approx(bgl::idl::cSoleClampRadians).margin(1e-3));

		// Short of the slope by exactly what the clamp withheld.
		const float shortfall =
			std::acos(std::clamp(glm::dot(posed.SoleNormal(), normal), -1.0f, 1.0f));
		CHECK(shortfall == Catch::Approx(radians - bgl::idl::cSoleClampRadians).margin(1e-3));
	}
}

TEST_CASE(
	"with planting off a rig with legs poses as one without",
	"[skinned][pose][plant][render]")
{
	const Posed off = PoseLeg(c_Flat, 255, { .plantFeet = false });

	for (uint32_t bone = 0; bone < c_Bones; ++bone)
	{
		INFO("bone " << bone);
		bgl::test::CheckNear(off.Bone(bone), c_Bind[bone]);
	}
}

TEST_CASE("what hangs off a planted foot follows it", "[skinned][pose][plant][render]")
{
	const Posed posed = PoseLeg(c_Flat, 255);

	// The toe is not solved; it is carried. So it must hold its bind offset from the ankle exactly,
	// and it must have moved -- a fixup that did nothing would leave it at its bind position while
	// the ankle rose, which is the failure this pins.
	const glm::vec3 offset = posed.Bone(c_Toe) - posed.Bone(c_Ankle);
	bgl::test::CheckNear(offset, c_Bind[c_Toe] - c_Bind[c_Ankle]);

	CHECK(posed.Bone(c_Toe).y == Catch::Approx(c_FootHeight).margin(1e-4));
}

TEST_CASE(
	"the ground is sampled where the instance stands, not where the rig is authored",
	"[skinned][pose][plant][render]")
{
	// Lifted: the plane stays at world y = 0, so in the rig's own space it is now that far *below*
	// the origin and the foot has to reach down to it.
	constexpr float c_Lift = 0.2f;
	const auto      world  = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, c_Lift, 0.0f));

	const Posed posed = PoseLeg(c_Flat, 255);
	const Posed moved = PoseLeg(c_Flat, 255, { .world = world });

	// A pass that sampled the ground in the rig's own space would put both soles in the same place.
	CHECK(posed.Sole().y == Catch::Approx(0.0f).margin(1e-4));
	CHECK(moved.Sole().y == Catch::Approx(-c_Lift).margin(1e-3));
}

// ADR-6: an instance's transform is fixed for its lifetime and the ground stands, so both palettes
// solve against the same placement against the same plane -- and the pose `prevTime` produces is the
// pose that was drawn. A planted foot writing a velocity it never moved through is what this rules
// out; the motion-vector suite proves the other end of it.
TEST_CASE(
	"a held, planted instance poses the same at prevTime as at time",
	"[skinned][pose][plant][render]")
{
	const Posed posed = PoseLeg(c_Flat, 255);

	const size_t stride = size_t(bgl::idl::cFloat4sPerBone) * c_Bones;
	REQUIRE(posed.palette.rows.size() == 2 * stride);

	for (size_t row = 0; row < stride; ++row)
	{
		INFO("row " << row);
		const glm::vec4 now  = posed.palette.rows[row];
		const glm::vec4 then = posed.palette.rows[stride + row];

		CHECK(now.x == Catch::Approx(then.x).margin(1e-5));
		CHECK(now.y == Catch::Approx(then.y).margin(1e-5));
		CHECK(now.z == Catch::Approx(then.z).margin(1e-5));
		CHECK(now.w == Catch::Approx(then.w).margin(1e-5));
	}
}

namespace
{
	constexpr uint16_t c_RenderStride = 64;

	void
	PutFloats(std::vector<std::byte>& bytes, size_t at, std::span<const float> values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(float));
	}

	void
	PutU16x4(std::vector<std::byte>& bytes, size_t at, const std::array<uint16_t, 4>& values)
	{
		std::memcpy(bytes.data() + at, values.data(), values.size() * sizeof(uint16_t));
	}

	/**
	 * A quad around the ankle, bound entirely to it, in the full renderable layout. Bound to one
	 * bone on purpose: what this draws is where the plant put the foot, and a strip spanning two
	 * bones would mix that with the shin.
	 */
	assetlib::BMesh
	MakeFootQuad()
	{
		const std::array<glm::vec3, 4> positions = { {
			{ -0.4f, -0.3f, 0.0f },
			{ 0.4f, -0.3f, 0.0f },
			{ -0.4f, 0.3f, 0.0f },
			{ 0.4f, 0.3f, 0.0f },
		} };

		auto mesh = assetlib::BMesh();
		mesh.vertexData.assign(size_t(4) * c_RenderStride, std::byte{ 0 });

		for (uint32_t v = 0; v < 4; ++v)
		{
			const size_t base = size_t(v) * c_RenderStride;

			const std::array<float, 3> pos = { { positions[v].x, positions[v].y, positions[v].z } };
			const std::array<float, 3> normal = { { 0.0f, 0.0f, 1.0f } };
			const std::array<float, 2> uv  = { { positions[v].x + 0.5f, positions[v].y + 0.5f } };
			const std::array<float, 4> tan = { { 1.0f, 0.0f, 0.0f, 1.0f } };

			PutFloats(mesh.vertexData, base + 0, pos);
			PutFloats(mesh.vertexData, base + 12, normal);
			PutFloats(mesh.vertexData, base + 24, uv);
			PutFloats(mesh.vertexData, base + 32, tan);

			// unorm16 0xFFFF is exactly 1.0.
			PutU16x4(mesh.vertexData, base + 48, { { uint16_t(c_Ankle), 0, 0, 0 } });
			PutU16x4(mesh.vertexData, base + 56, { { 0xFFFF, 0, 0, 0 } });
		}

		auto meshlet           = assetlib::Meshlet();
		meshlet.vertexCount    = 4;
		meshlet.triangleCount  = 2;
		meshlet.boundingRadius = 4.0f;
		mesh.meshlets.push_back(meshlet);

		for (uint32_t v = 0; v < 4; ++v) mesh.meshletVertices.push_back(v);
		// Typed, not a braced list of ints: that deduces initializer_list<int> and narrows on the
		// way out, which MSVC treats as an error.
		constexpr std::array<uint8_t, 6> c_Indices = { { 0, 1, 2, 2, 1, 3 } };
		for (uint8_t i : c_Indices) mesh.meshletTriangles.push_back(i);

		auto submesh                  = assetlib::Submesh();
		submesh.layout.attributeCount = 6;
		submesh.layout.stride         = c_RenderStride;
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
		submesh.aabbMin               = glm::vec3(-3.0f);
		submesh.aabbMax               = glm::vec3(3.0f);
		mesh.submeshes.push_back(submesh);

		auto entry         = assetlib::Mesh();
		entry.submeshCount = 1;
		mesh.meshes.push_back(entry);

		return mesh;
	}
}

// The other end of ADR-6, through the draw rather than off the palette: a foot that is planted and
// held must write no velocity, because both palettes solved against one placement on one plane. A
// plant that read the clock, or a ground that moved between the two, would shimmer here.
TEST_CASE(
	"a planted, held foot writes no velocity on a slope",
	"[skinned][pose][plant][motionvectors][render]")
{
	constexpr uint32_t c_Width  = 128;
	constexpr uint32_t c_Height = 128;

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

	const float radians = glm::radians(15.0f);
	scene->SetGround({ glm::vec3(0.0f), glm::vec3(std::sin(radians), std::cos(radians), 0.0f) });

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(
		bgl::PbrMaterialDesc()) } };

	const bgl::RigHandle rig = scene->AddRig(MakeLegRig(), MakeStillClip(), MakeLeg(255));
	REQUIRE(rig.IsValid());

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeFootQuad(),
		0,
		materials,
		rig,
		assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) });
	REQUIRE(geom.IsValid());

	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

	// Two frames either way: prevTime equals time on the first by construction, so only the second
	// can carry a velocity at all.
	const auto peakVelocity = [&](float rate) {
		auto localView = gfx->CreateSceneView(scene, 4);
		bgl::test::ApplyEnvironment(scene.Get(), localView.Get());
		localView->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, rate });

		auto job     = bgl::RenderJob();
		job.view     = localView;
		job.camera   = camera;
		job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));

		job.time = 0.0f;
		gfx->DrawFrame(target, job);

		job.time = 1.0f / 30.0f;
		gfx->DrawFrame(target, job);

		float peak = 0.0f;
		for (const glm::vec2& v :
		     bgl::test::ReadMotionVectors(gfx.Get(), target.Get(), c_Width, c_Height))
		{
			peak = std::max(peak, glm::length(v));
		}
		return peak;
	};

	const float animating = peakVelocity(1.0f);
	const float held      = peakVelocity(0.0f);

	INFO("animating peak = " << animating << ", held peak = " << held);

	// The control: the case can see motion at all, so the zero below is a fact about the plant and
	// not about the camera or the readback.
	CHECK(animating > 1e-3f);
	CHECK(held < 1e-5f);
}

// The one check the palette cannot make: that the planted pose reaches the screen looking right.
// A readback pins where the sole is to four decimal places and still says nothing about a foot
// clipping through the slope it stands on, or about the silhouette the new bone matrices skin.
TEST_CASE("a planted foot on a slope draws", "[skinned][pose][plant][render]")
{
	constexpr uint32_t c_Width  = 256;
	constexpr uint32_t c_Height = 256;

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

	const float radians = glm::radians(15.0f);
	scene->SetGround({ glm::vec3(0.0f), glm::vec3(std::sin(radians), std::cos(radians), 0.0f) });

	auto view = gfx->CreateSceneView(scene, 4);
	bgl::test::ApplyEnvironment(scene.Get(), view.Get());

	auto material            = bgl::PbrMaterialDesc();
	material.metallicFactor  = 0.0f;
	material.roughnessFactor = 0.6f;

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(material) } };

	const bgl::RigHandle rig = scene->AddRig(MakeLegRig(), MakeStillClip(), MakeLeg(255));
	REQUIRE(rig.IsValid());

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeFootQuad(),
		0,
		materials,
		rig,
		assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) });
	REQUIRE(geom.IsValid());

	view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });

	auto camera = bgl::Camera();
	camera.LookAt(glm::vec3(0.0f, 0.3f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

	auto job     = bgl::RenderJob();
	job.view     = view;
	job.camera   = camera;
	job.viewport = bgl::Viewport(static_cast<float>(c_Width), static_cast<float>(c_Height));
	gfx->DrawFrame(target, job);

	gfx->ScreenshotPng(target, "assets/golden/foot_plant_slope.got.png");

	// The quad is bound wholly to the ankle, so its centre is where the plant put the foot: lifted
	// and turned onto the slope. A sample that hit the background instead would mean the solve
	// carried the foot out of frame.
	const bgl::test::Rgba foot =
		bgl::test::MeanColor("assets/golden/foot_plant_slope.got.png", 118, 118, 20, 20);
	CHECK(foot.Luma() > 0.01f);

	CHECK(
		bgl::test::MatchesGolden(
			"assets/golden/foot_plant_slope.exp.png",
			"assets/golden/foot_plant_slope.got.png"));
}

namespace
{
	// A pelvis with two legs hanging off it, each the same shape as the one-leg rig and mirrored
	// across X. Two, because the drop is a property of the *rig*: the largest deficit across every
	// leg is what the whole body comes down by, and one leg cannot show that.
	constexpr uint32_t c_TwoLegBones = 9;

	// The ankles are splayed outward from the hips, and by different amounts, which lets this rig
	// say two things a straight-down rig cannot. Different amounts, so the legs fall short by
	// different distances and the *largest* is observably what the body comes down by. Splayed at
	// all, because a target off to one side of its hip is the case a drop sized as the straight-line
	// shortfall gets wrong -- and on flat ground the target does not move when the rig drops, so
	// nothing downstream rescues an estimate that came up short.
	//
	// Lifted by c_FootHeight for the same reason c_Bind is: both soles rest on y = 0, the floor a
	// grounded clip stands on and the one the plant measures departure from.
	const std::array<glm::vec3, c_TwoLegBones> c_TwoLegBind = { {
		glm::vec3(0.0f, 2.0f + c_FootHeight, 0.0f),   // 0 pelvis
		glm::vec3(-0.4f, 2.0f + c_FootHeight, 0.0f),  // 1 hip   L
		glm::vec3(-0.3f, 1.0f + c_FootHeight, 0.0f),  // 2 knee  L
		glm::vec3(-1.0f, 0.0f + c_FootHeight, 0.0f),  // 3 ankle L
		glm::vec3(-1.2f, 0.0f + c_FootHeight, 0.0f),  // 4 toe   L
		glm::vec3(0.4f, 2.0f + c_FootHeight, 0.0f),   // 5 hip   R
		glm::vec3(0.5f, 1.0f + c_FootHeight, 0.0f),   // 6 knee  R
		glm::vec3(1.3f, 0.0f + c_FootHeight, 0.0f),   // 7 ankle R
		glm::vec3(1.5f, 0.0f + c_FootHeight, 0.0f),   // 8 toe   R
	} };

	const std::array<uint32_t, c_TwoLegBones> c_TwoLegParent = { {
		assetlib::c_InvalidIndex,
		0,
		1,
		2,
		3,
		0,
		5,
		6,
		7,
	} };

	assetlib::Skeleton
	MakeTwoLegRig()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_TwoLegBones; ++i)
		{
			const uint32_t  parent = c_TwoLegParent[i];
			const glm::vec3 origin =
				parent == assetlib::c_InvalidIndex ? glm::vec3(0.0f) : c_TwoLegBind[parent];

			auto bone        = assetlib::Bone();
			bone.bindPose    = { c_TwoLegBind[i] - origin,
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), -c_TwoLegBind[i]);
			bone.parent      = parent;
			bone.nameOffset  = skeleton.stringPool.add(std::format("Bone{}", i));
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	assetlib::AnimationSet
	MakeTwoLegClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_TwoLegBones;

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_TwoLegBones; ++b)
			{
				const uint32_t  parent = c_TwoLegParent[b];
				const glm::vec3 origin =
					parent == assetlib::c_InvalidIndex ? glm::vec3(0.0f) : c_TwoLegBind[parent];

				auto sample        = assetlib::Transform();
				sample.translation = c_TwoLegBind[b] - origin;
				sample.rotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale       = glm::vec3(1.0f);
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
	MakeTwoLegs(uint8_t weight)
	{
		auto plant = bgl::FootPlantDesc();
		for (uint32_t hip : { 1u, 5u })
		{
			auto leg       = bgl::FootPlantLegDesc();
			leg.hip        = hip;
			leg.knee       = hip + 1;
			leg.ankle      = hip + 2;
			leg.toe        = hip + 3;
			leg.solePoint  = c_SolePoint;
			leg.soleNormal = c_SoleNormal;
			plant.legs.push_back(leg);
		}
		plant.plantWeights = std::vector<uint8_t>(size_t(c_Frames) * 2, weight);
		return plant;
	}

	/** The two-leg rig posed against `ground`, read back. */
	bgl::test::Palette
	PoseTwoLegs(const bgl::GroundPlaneDesc& ground)
	{
		auto opts             = bgl::GraphicsOptions();
		opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
		opts.enableDebugLayer = true;

		auto gfx = bgl::CreateGraphics(opts);
		REQUIRE(gfx != nullptr);

		auto* gfxBase = gfx->As<bgl::GraphicsBase>();
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
		scene->SetGround(ground);

		auto view = gfx->CreateSceneView(scene, 4);
		bgl::test::ApplyEnvironment(scene.Get(), view.Get());

		const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(
			bgl::PbrMaterialDesc()) } };

		const bgl::RigHandle rig =
			scene->AddRig(MakeTwoLegRig(), MakeTwoLegClip(), MakeTwoLegs(255));
		REQUIRE(rig.IsValid());

		const auto geom = scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			rig,
			assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) });
		REQUIRE(geom.IsValid());

		auto* viewRaw = view->As<bgl::SceneView>();
		REQUIRE(viewRaw != nullptr);

		const auto instance =
			view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = bgl::Viewport(64.0f, 64.0f);
		gfx->DrawFrame(target, job);

		return bgl::test::ReadPalette(
			gfxBase,
			viewRaw,
			bgl::test::PaletteBaseOf(viewRaw, instance),
			bgl::idl::cFloat4sPerBone * c_TwoLegBones);
	}
}

TEST_CASE(
	"a rig whose legs cannot reach comes down to meet the ground",
	"[skinned][pose][plant][render]")
{
	// Both legs reach about 2.2 from a hip standing 2 above the origin, so a floor at -0.5 is past
	// either of them -- and past the right one, whose ankle is splayed further out, by the more.
	constexpr float c_Floor = -0.5f;

	const auto ground =
		bgl::GroundPlaneDesc{ glm::vec3(0.0f, c_Floor, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) };

	const bgl::test::Palette posed = PoseTwoLegs(ground);

	const auto sole = [&](uint32_t ankle) {
		return posed.Apply(ankle, c_TwoLegBind[ankle] + c_SolePoint);
	};

	SECTION("both soles rest on the floor, so the drop was the larger leg's")
	{
		// The right leg needs the bigger drop; sized by the left one it would still hang above the
		// floor. Sized as a straight-line shortfall it would hang there too, by less -- which is
		// what makes this an assertion about the drop and not merely about planting.
		CHECK(sole(3).y == Catch::Approx(c_Floor).margin(3e-3));
		CHECK(sole(7).y == Catch::Approx(c_Floor).margin(3e-3));
	}

	SECTION("the rig came straight down, and it did move")
	{
		const glm::vec3 moved = posed.Apply(0, c_TwoLegBind[0]) - c_TwoLegBind[0];

		REQUIRE(moved.y < -1e-3f);
		CHECK(moved.x == Catch::Approx(0.0f).margin(1e-4));
		CHECK(moved.z == Catch::Approx(0.0f).margin(1e-4));
	}

	SECTION("a rig that can reach is not lowered at all")
	{
		// The same rig on a floor inside its reach: the legs stretch and the pelvis stays put, which
		// is what makes the drop above a consequence of the deficit rather than of planting at all.
		const bgl::test::Palette easy = PoseTwoLegs(
			bgl::GroundPlaneDesc{ glm::vec3(0.0f, -0.1f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f) });

		bgl::test::CheckNear(easy.Apply(0, c_TwoLegBind[0]), c_TwoLegBind[0]);
		CHECK(easy.Apply(3, c_TwoLegBind[3] + c_SolePoint).y == Catch::Approx(-0.1f).margin(1e-3));
		CHECK(easy.Apply(7, c_TwoLegBind[7] + c_SolePoint).y == Catch::Approx(-0.1f).margin(1e-3));
	}
}

// A slope gives the two legs different work to do: the feet sit either side of the centre line, so
// one is much further above the ground than the other. That is what makes the *largest* deficit the
// subject -- on a flat floor both legs need the same drop and a solve that used each leg's own
// would look identical.
//
// It is also the geometry a drop sized as the straight-line shortfall gets wrong: the target is off
// to one side of the hip, so dropping by `|v| - reach` leaves the foot short. Both soles landing on
// the plane is what says the drop is solved for rather than estimated.
TEST_CASE(
	"on a slope the whole rig drops by the leg that falls furthest short",
	"[skinned][pose][plant][render]")
{
	const float radians = glm::radians(15.0f);
	const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
	const auto  origin  = glm::vec3(0.0f, -0.6f, 0.0f);

	const bgl::test::Palette posed = PoseTwoLegs(bgl::GroundPlaneDesc{ origin, normal });

	const auto sole = [&](uint32_t ankle) {
		return posed.Apply(ankle, c_TwoLegBind[ankle] + c_SolePoint);
	};

	// On the plane, both of them. The right foot starts much further above it than the left, so a
	// drop sized by the left leg would leave the right one hanging.
	CHECK(glm::dot(sole(3) - origin, normal) == Catch::Approx(0.0f).margin(3e-3));
	CHECK(glm::dot(sole(7) - origin, normal) == Catch::Approx(0.0f).margin(3e-3));

	// The rig came down along the ground's up and nothing else, so the pelvis moved antiparallel to
	// the normal rather than straight down.
	const glm::vec3 moved = posed.Apply(0, c_TwoLegBind[0]) - c_TwoLegBind[0];
	REQUIRE(glm::length(moved) > 1e-3f);
	CHECK(glm::dot(glm::normalize(moved), normal) == Catch::Approx(-1.0f).margin(1e-3));
}

// The runtime weight a caller sets over the baked plant: one FootIKLeg per leg of the rig, in an
// arena of the view's, reached through the pose list. Nothing on the GPU reads it yet; these cases
// pin the record itself -- what a spawn writes, what a write stores, and what a delete frees.

namespace
{
	bgl::WeightRamp
	Ramp(float from, float to, float start, float end)
	{
		return { from, to, start, end };
	}

	void
	CheckRamp(const bgl::WeightRamp& actual, const bgl::WeightRamp& expected)
	{
		CHECK(actual.from == expected.from);
		CHECK(actual.to == expected.to);
		CHECK(actual.start == expected.start);
		CHECK(actual.end == expected.end);
	}
}

TEST_CASE("a hero instance's foot-IK record starts at weight one", "[skinned][plant][footik]")
{
	const LegScene legScene = MakeLegScene(c_Flat, 255);
	auto&          view     = legScene.view;

	const auto instance =
		view->CreateSkinnedMeshInstance(legScene.geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });

	const bgl::FootIKDesc read = view->GetFootIK(instance);
	for (const bgl::FootIKLegDesc& leg : read.leg)
	{
		CheckRamp(leg.position, bgl::WeightRamp());
		CheckRamp(leg.rotation, bgl::WeightRamp());
	}

	// The pose list carries the record beside the placement, sized by the rig's legs: this is the
	// one place the pose pass will read it from.
	auto* viewRaw = view->As<bgl::SceneView>();
	REQUIRE(viewRaw != nullptr);
	const auto& meta = viewRaw->GetMeshBuffer().MetaAt(instance.handle.index);
	REQUIRE(meta.footIK);
	CHECK(meta.footIK.count == 1);

	SECTION("a write stores every field of the rig's legs and reads them back")
	{
		auto desc            = bgl::FootIKDesc();
		desc.leg[0].position = Ramp(1.0f, 0.25f, 2.0f, 2.5f);
		desc.leg[0].rotation = Ramp(0.0f, 1.0f, 3.0f, 3.0f);

		// Past the rig's one leg: not stored, and read back as the default.
		desc.leg[1].position = Ramp(0.5f, 0.5f, 0.0f, 0.0f);

		view->SetFootIK(instance, desc);

		const bgl::FootIKDesc stored = view->GetFootIK(instance);
		CheckRamp(stored.leg[0].position, desc.leg[0].position);
		CheckRamp(stored.leg[0].rotation, desc.leg[0].rotation);
		CheckRamp(stored.leg[1].position, bgl::WeightRamp());

		const bgl::idl::FootIKLeg record = viewRaw->GetFootIKArena().Get(meta.footIK, 0);
		CHECK(record.position.to == 0.25f);
		CHECK(record.rotation.start == 3.0f);
	}

	SECTION("a refused write leaves the record as it was")
	{
		const auto before = bgl::FootIKDesc::Constant(0.5f, 0.5f);
		view->SetFootIK(instance, before);

		auto outside            = before;
		outside.leg[0].position = Ramp(0.0f, 1.5f, 0.0f, 0.0f);
		CHECK_THROWS_AS(view->SetFootIK(instance, outside), bgl::SceneError);

		auto negative            = before;
		negative.leg[0].rotation = Ramp(-0.1f, 1.0f, 0.0f, 0.0f);
		CHECK_THROWS_AS(view->SetFootIK(instance, negative), bgl::SceneError);

		auto backwards            = before;
		backwards.leg[0].position = Ramp(0.0f, 1.0f, 2.0f, 1.0f);
		CHECK_THROWS_AS(view->SetFootIK(instance, backwards), bgl::SceneError);

		auto nan            = before;
		nan.leg[0].rotation = Ramp(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f, 0.0f);
		CHECK_THROWS_AS(view->SetFootIK(instance, nan), bgl::SceneError);

		// A field past the rig's legs is not judged, since it is not stored.
		auto ignored            = before;
		ignored.leg[1].position = Ramp(7.0f, 7.0f, 0.0f, 0.0f);
		CHECK_NOTHROW(view->SetFootIK(instance, ignored));

		const bgl::FootIKDesc stored = view->GetFootIK(instance);
		CheckRamp(stored.leg[0].position, before.leg[0].position);
		CheckRamp(stored.leg[0].rotation, before.leg[0].rotation);
	}

	SECTION("a placement without a record is refused")
	{
		auto crowd       = bgl::SkinnedInstanceDesc();
		crowd.source     = bgl::PoseSource::kBoneAnimTable;
		const auto table = view->CreateSkinnedMeshInstance(legScene.geom, glm::mat4(1.0f), crowd);
		CHECK_THROWS_AS(view->GetFootIK(table), bgl::SceneError);
		CHECK_THROWS_AS(view->SetFootIK(table, bgl::FootIKDesc()), bgl::SceneError);

		const auto cube = legScene.scene->AddCubeGeom(bgl::MaterialHandle());
		const auto stat = view->CreateStaticMeshInstance(cube, glm::mat4(1.0f));
		CHECK_THROWS_AS(view->GetFootIK(stat), bgl::SceneError);

		view->DeleteMeshInstance(instance);
		CHECK_THROWS_AS(view->GetFootIK(instance), bgl::SceneError);
	}

	SECTION("deleting the instance frees its record, and the next spawn starts clean")
	{
		view->SetFootIK(instance, bgl::FootIKDesc::Constant(0.0f, 0.0f));
		const auto handle = meta.footIK;
		view->DeleteMeshInstance(instance);
		CHECK_FALSE(viewRaw->GetFootIKArena().IsValid(handle));

		const auto next =
			view->CreateSkinnedMeshInstance(legScene.geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });
		const bgl::FootIKDesc fresh = view->GetFootIK(next);
		CheckRamp(fresh.leg[0].position, bgl::WeightRamp());
		CheckRamp(fresh.leg[0].rotation, bgl::WeightRamp());
	}
}

TEST_CASE("a rig without legs owns no foot-IK record", "[skinned][plant][footik]")
{
	auto opts             = bgl::GraphicsOptions();
	opts.shaderCacheDir   = bgl::test::ShaderCacheDir();
	opts.enableDebugLayer = true;
	auto gfx              = bgl::CreateGraphics(opts);
	REQUIRE(gfx != nullptr);

	auto sceneDesc                        = bgl::SceneDesc();
	sceneDesc.initialGeom                 = 4;
	sceneDesc.initialMeshlets             = 8;
	sceneDesc.initialSubmeshes            = 4;
	sceneDesc.initialVertexBufferByteSize = 4096;
	sceneDesc.initialIndices              = 64;
	sceneDesc.initialPbrMaterials         = 4;
	auto scene                            = gfx->CreateScene(sceneDesc);
	auto view                             = gfx->CreateSceneView(scene, 4);

	const std::array<bgl::MaterialHandle, 1> materials = { { scene->CreatePbrMaterial(
		bgl::PbrMaterialDesc()) } };
	const bgl::RigHandle                     rig  = scene->AddRig(MakeLegRig(), MakeStillClip());
	const auto                               geom = scene->AddSkinnedMeshGeom(
		MakeSkinnedTriangle(),
		0,
		materials,
		rig,
		assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) });
	REQUIRE(geom.IsValid());

	const auto instance = view->CreateSkinnedMeshInstance(geom, glm::mat4(1.0f), { 0, 0.0f, 0.0f });

	auto* viewRaw = view->As<bgl::SceneView>();
	REQUIRE(viewRaw != nullptr);
	CHECK_FALSE(viewRaw->GetMeshBuffer().MetaAt(instance.handle.index).footIK);

	CHECK_THROWS_AS(view->GetFootIK(instance), bgl::SceneError);
	CHECK_THROWS_AS(view->SetFootIK(instance, bgl::FootIKDesc()), bgl::SceneError);
}

TEST_CASE("a weight ramp reads as the shader will", "[skinned][plant][footik]")
{
	const auto ramp = Ramp(0.2f, 1.0f, 2.0f, 4.0f);
	CHECK(ramp.At(1.0f) == 0.2f);
	CHECK(ramp.At(2.0f) == 0.2f);
	CHECK(ramp.At(3.0f) == Catch::Approx(0.6f));
	CHECK(ramp.At(4.0f) == 1.0f);
	CHECK(ramp.At(9.0f) == 1.0f);

	// A window of no width is a step at its start.
	const auto step = Ramp(0.0f, 1.0f, 5.0f, 5.0f);
	CHECK(step.At(4.999f) == 0.0f);
	CHECK(step.At(5.0f) == 1.0f);

	// A fade starts from what the ramp holds now, so a write built from it changes nothing before
	// now -- which is what keeps the pose the previous frame drew, and its motion vector, exact.
	const auto fade = ramp.FadeTo(0.0f, 3.0f, 1.0f);
	CheckRamp(fade, Ramp(0.6f, 0.0f, 3.0f, 4.0f));

	const auto whole =
		bgl::FootIKDesc::FadeTo(bgl::FootIKDesc::Constant(1.0f, 0.5f), 3.0f, 0.25f, 0.0f, 0.0f);
	CheckRamp(whole.leg[3].position, Ramp(1.0f, 0.0f, 3.0f, 3.25f));
	CheckRamp(whole.leg[3].rotation, Ramp(0.5f, 0.0f, 3.0f, 3.25f));
}
