#include "gfx/GraphicsBase.h"
#include "scene/Scene.h"
#include "scene/SceneView.h"
#include "util/GoldenImage.h"
#include "util/PaletteReadback.h"
#include "util/TestEnvironment.h"
#include "util/TestOptions.h"
#include "util/VelocityReadback.h"
#include <assetlib_structs/Bounds.h>
#include <bgl/Camera.h>
#include <bgl/IGraphics.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
	const std::array<glm::vec3, c_Bones> c_Bind = { {
		glm::vec3(0.0f, 2.0f, 0.0f),  // pelvis
		glm::vec3(0.0f, 2.0f, 0.0f),  // hip
		glm::vec3(0.6f, 1.0f, 0.0f),  // knee
		glm::vec3(0.0f, 0.0f, 0.0f),  // ankle
		glm::vec3(0.2f, 0.0f, 0.0f),  // toe
	} };

	assetlib::Skeleton
	MakeLegRig()
	{
		auto skeleton = assetlib::Skeleton();
		for (uint32_t i = 0; i < c_Bones; ++i)
		{
			const glm::vec3 parent = i == 0 ? glm::vec3(0.0f) : c_Bind[i - 1];

			auto bone        = assetlib::Bone();
			bone.bindPose    = { c_Bind[i] - parent,
				                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                 glm::vec3(1.0f) };
			bone.inverseBind = glm::translate(glm::mat4(1.0f), -c_Bind[i]);
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
	MakeStillClip()
	{
		auto set      = assetlib::AnimationSet();
		set.boneCount = c_Bones;

		for (uint32_t f = 0; f < c_Frames; ++f)
		{
			for (uint32_t b = 0; b < c_Bones; ++b)
			{
				const glm::vec3 parent = b == 0 ? glm::vec3(0.0f) : c_Bind[b - 1];

				// Rz(25) as w-first, on the hip of frame 1 alone.
				const auto swung = glm::quat(0.97437f, 0.0f, 0.0f, 0.22495f);

				auto sample        = assetlib::Transform();
				sample.translation = c_Bind[b] - parent;
				sample.rotation =
					(f == 1 && b == c_Hip) ? swung : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
				sample.scale = glm::vec3(1.0f);
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
	MakeLeg(uint8_t weight, const glm::vec3& solePoint = c_SolePoint)
	{
		auto leg       = bgl::FootPlantLegDesc();
		leg.hip        = c_Hip;
		leg.knee       = c_Knee;
		leg.ankle      = c_Ankle;
		leg.toe        = c_Toe;
		leg.solePoint  = solePoint;
		leg.soleNormal = c_SoleNormal;

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
		glm::vec3 solePoint = c_SolePoint;

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
				palette.Apply(c_Ankle, c_Bind[c_Ankle] + solePoint + c_SoleNormal) - Sole());
		}
	};

	Posed
	PoseLeg(
		const bgl::GroundPlaneDesc& ground,
		uint8_t                     weight,
		const glm::mat4&            world     = glm::mat4(1.0f),
		const glm::vec3&            solePoint = c_SolePoint)
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

		const auto geom = scene->AddSkinnedMeshGeom(
			MakeSkinnedTriangle(),
			0,
			materials,
			MakeLegRig(),
			MakeStillClip(),
			assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) },
			MakeLeg(weight, solePoint));
		REQUIRE(geom.IsValid());

		auto* viewRaw = view->As<bgl::SceneView>();
		REQUIRE(viewRaw != nullptr);

		// rate 0, so the clip holds frame 0 and the clock cannot move the pose between the two
		// palettes -- the solve is then the only thing that differs from the bind pose.
		const auto instance = view->CreateSkinnedMeshInstance(geom, world, { 0, 0.0f, 0.0f });

		auto job     = bgl::RenderJob();
		job.view     = view;
		job.viewport = bgl::Viewport(64.0f, 64.0f);
		gfx->DrawFrame(target, job);

		auto posed      = Posed();
		posed.solePoint = solePoint;
		posed.palette   = bgl::test::ReadPalette(
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
	SECTION("flat ground lifts the sole onto the plane")
	{
		const Posed posed = PoseLeg(c_Flat, 255);

		// The sole's bind position is c_FootHeight below the plane, so a solve that did nothing
		// would leave it there.
		bgl::test::CheckNear(posed.Sole(), glm::vec3(0.0f, 0.0f, 0.0f));

		// The ankle carries the whole foot's height above the contact.
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
		const Posed full = PoseLeg(c_Flat, 255);
		const Posed half = PoseLeg(c_Flat, 128);

		// 128/255 of the way, which is what a weight interpolates -- not a threshold.
		const float fraction = 128.0f / 255.0f;
		CHECK(half.Bone(c_Ankle).y == Catch::Approx(fraction * full.Bone(c_Ankle).y).margin(2e-3));
	}
}

TEST_CASE("a planted foot turns onto the slope it stands on", "[skinned][pose][plant][render]")
{
	SECTION("the sole normal comes onto the ground normal, and the sole stays on the plane")
	{
		const float radians = glm::radians(15.0f);
		const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
		const auto  slope   = bgl::GroundPlaneDesc{ glm::vec3(0.0f), normal };

		const Posed posed = PoseLeg(slope, 255);

		bgl::test::CheckNear(posed.SoleNormal(), normal);

		// On the plane, which for a slope through the origin means perpendicular to its normal.
		CHECK(glm::dot(posed.Sole(), normal) == Catch::Approx(0.0f).margin(1e-4));
	}

	SECTION("past thirty degrees the ankle stops turning")
	{
		// The sole point sits on the ankle joint and the plane passes through it, so the position
		// target is the joint itself and the chain does not move at all. What is left is the tilt
		// alone, which is the only way to read the clamp off a palette: anywhere else the shin's own
		// rotation is folded into the same number.
		const float radians = glm::radians(45.0f);
		const auto  normal  = glm::vec3(std::sin(radians), std::cos(radians), 0.0f);
		const auto  slope   = bgl::GroundPlaneDesc{ glm::vec3(0.0f), normal };

		const Posed posed = PoseLeg(slope, 255, glm::mat4(1.0f), glm::vec3(0.0f));
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
	const Posed moved = PoseLeg(c_Flat, 255, world);

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

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeFootQuad(),
		0,
		materials,
		MakeLegRig(),
		MakeStillClip(),
		assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) },
		MakeLeg(255));
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

	const auto geom = scene->AddSkinnedMeshGeom(
		MakeFootQuad(),
		0,
		materials,
		MakeLegRig(),
		MakeStillClip(),
		assetlib::Bounds{ glm::vec3(-8.0f), glm::vec3(8.0f) },
		MakeLeg(255));
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
