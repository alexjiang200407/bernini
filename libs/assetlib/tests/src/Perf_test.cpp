#include <algorithm>
#include <array>
#include <assetlib/AssetStore.h>
#include <assetlib/bmesh.h>
#include <assetlib/codecs.h>
#include <assetlib/rebake_bounds.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <assetlib_structs/Skeleton.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <ratio>
#include <span>

#include "CountingFileSystem.h"
#include "MountAt.h"
#include <assetlib_structs/Node.h>
#include <assetlib_structs/VertexLayout.h>

/**
 * What a cook costs as its inputs grow.
 *
 * These do not assert a wall-clock ceiling. A budget loose enough to survive a loaded machine and a
 * debug build is loose enough to miss a 3x regression, and the number would differ on every
 * developer's desk. What each case pins instead is a *shape*: a count that must not grow with an
 * input, or a ratio between two problem sizes that must stay far below the ratio of the sizes. Both
 * are properties of the algorithm rather than of the machine, so they hold in debug, under load, and
 * on a runner.
 *
 * They run at a reduced scale on purpose. A 663-bone, 2254-frame rig is minutes, and a suite
 * `just test` runs cannot be minutes -- a scaling shape proved at one base size holds at another.
 * The real dimensions are in docs/skinning.md.
 */

using namespace assetlib;
using namespace assetlib::test;

namespace
{
	namespace fs = std::filesystem;

	constexpr uint32_t c_Bones  = 128;
	constexpr uint32_t c_Frames = 300;

	/** The fastest of `runs`, which is the measurement least polluted by whatever else is running. */
	template <typename Fn>
	double
	FastestMillis(const int runs, Fn&& body)
	{
		auto best = std::numeric_limits<double>::max();
		for (int i = 0; i < runs; ++i)
		{
			const auto start = std::chrono::steady_clock::now();
			body();
			const auto ms =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
					.count();
			best = std::min(best, ms);
		}
		return best;
	}

	/** A flat rig: every bone a child of the root, so a pose walk costs one pass over `bones`. */
	Skeleton
	MakeRig(const uint32_t bones)
	{
		auto skeleton = Skeleton();

		auto root        = Bone();
		root.bindPose    = { glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f) };
		root.parent      = c_InvalidIndex;
		root.nameOffset  = skeleton.stringPool.add("root");
		root.inverseBind = glm::mat4(1.0f);
		skeleton.bones.push_back(root);

		for (uint32_t i = 1; i < bones; ++i)
		{
			auto bone       = Bone();
			bone.bindPose   = { glm::vec3(static_cast<float>(i), 0.0f, 0.0f),
				                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                glm::vec3(1.0f) };
			bone.parent     = 0;
			bone.nameOffset = skeleton.stringPool.add(std::format("bone{}", i));
			bone.inverseBind =
				glm::translate(glm::mat4(1.0f), glm::vec3(-static_cast<float>(i), 0.0f, 0.0f));
			skeleton.bones.push_back(bone);
		}
		return skeleton;
	}

	/** One clip of `frames`, every bone moving, so no pose can be skipped as constant. */
	AnimationSet
	MakeClips(const Skeleton& skeleton, const uint32_t frames)
	{
		auto animations              = AnimationSet();
		animations.skeleton          = "Derived/Skeletons/rig.bskel";
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());

		auto clip        = AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = frames;
		clip.sampleRate  = 30.0f;
		animations.clips.push_back(clip);

		for (uint32_t frame = 0; frame < frames; ++frame)
			for (const Bone& bone : skeleton.bones)
			{
				Transform sample = bone.bindPose;
				sample.translation.y += static_cast<float>(frame) * 0.01f;
				animations.samples.push_back(sample);
			}
		return animations;
	}

	/**
	 * `entries` mesh entries, each welded to two bones.
	 *
	 * Two rather than all of them on purpose: an entry sweeps only the bones it has weight on, so a
	 * rig whose every entry touched every bone would make the per-entry work dominate and hide the
	 * shared pose walk these cases are about -- which is also how a real rig is shaped.
	 */
	BMesh
	MakeMesh(const uint32_t entries, const Skeleton& skeleton)
	{
		const auto bones = static_cast<uint32_t>(skeleton.bones.size());

		auto mesh              = BMesh();
		mesh.skeleton          = "Derived/Skeletons/rig.bskel";
		mesh.skeletonSignature = skeletonSignature(skeleton);

		for (uint32_t entry = 0; entry < entries; ++entry)
		{
			auto submesh                  = Submesh();
			submesh.layout.attributeCount = 3;
			submesh.layout.attributes[0]  = { VertexSemantic::kPosition,
				                              VertexFormat::kFloat32x3,
				                              0 };
			submesh.layout.attributes[1]  = { VertexSemantic::kJoints0,
				                              VertexFormat::kUint16x4,
				                              12 };
			submesh.layout.attributes[2]  = { VertexSemantic::kWeights0,
				                              VertexFormat::kUnorm16x4,
				                              20 };
			submesh.layout.stride         = 28;

			const auto first  = static_cast<uint16_t>((entry * 2) % bones);
			const auto second = static_cast<uint16_t>((entry * 2 + 1) % bones);

			for (const float sign : { -1.0f, 1.0f })
			{
				const size_t base = mesh.vertexData.size();
				mesh.vertexData.resize(base + submesh.layout.stride);

				const auto                    position = glm::vec3(sign, sign, sign);
				const std::array<uint16_t, 4> joints{ { first, second, 0, 0 } };
				const std::array<uint16_t, 4> weights{ { 32767, 32768, 0, 0 } };

				std::byte* at = mesh.vertexData.data() + base;
				std::memcpy(at, &position, sizeof(position));
				std::memcpy(at + 12, joints.data(), sizeof(joints));
				std::memcpy(at + 20, weights.data(), sizeof(weights));
				++submesh.vertexCount;
			}

			submesh.aabbMin = glm::vec3(-1.0f);
			submesh.aabbMax = glm::vec3(1.0f);

			mesh.submeshes.push_back(submesh);
			mesh.meshes.push_back({ .firstSubmesh = entry, .submeshCount = 1, .nameOffset = 0 });
		}
		return mesh;
	}

	/**
	 * A rig of two bones a vertex can hang from: a foot on the floor, and a second one `padHeight`
	 * above it. Both are rotated by the clip below, so a pose sweeps their boxes wider than the
	 * vertices inside them -- which is what stops the frame prune from cutting, and so what makes a
	 * clip floor expensive in the first place.
	 */
	Skeleton
	MakeFloorRig(const float padHeight)
	{
		auto skeleton = Skeleton();

		const auto bone = [&](const char* name, const float y) {
			auto out        = Bone();
			out.bindPose    = { glm::vec3(0.0f, y, 0.0f),
				                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
				                glm::vec3(1.0f) };
			out.parent      = skeleton.bones.empty() ? c_InvalidIndex : 0;
			out.nameOffset  = skeleton.stringPool.add(name);
			out.inverseBind = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -y, 0.0f));
			skeleton.bones.push_back(out);
		};

		bone("root", 0.0f);
		bone("foot", 0.0f);
		bone("pad", padHeight);
		return skeleton;
	}

	/**
	 * One clip that rotates every bone about z and holds that rotation, so each frame's bound ties
	 * with the last and none of them is pruned -- the shape a character standing still has.
	 */
	AnimationSet
	MakeStandingClip(const Skeleton& skeleton, const uint32_t frames)
	{
		auto animations              = AnimationSet();
		animations.skeleton          = "Derived/Skeletons/rig.bskel";
		animations.skeletonSignature = skeletonSignature(skeleton);
		animations.boneCount         = static_cast<uint32_t>(skeleton.bones.size());

		auto clip        = AnimationClip();
		clip.firstSample = 0;
		clip.frameCount  = frames;
		clip.sampleRate  = 30.0f;
		animations.clips.push_back(clip);

		for (uint32_t frame = 0; frame < frames; ++frame)
			for (const Bone& bone : skeleton.bones)
			{
				Transform sample = bone.bindPose;
				sample.rotation  = glm::angleAxis(
					glm::radians(45.0f),
					glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)));
				animations.samples.push_back(sample);
			}
		return animations;
	}

	/**
	 * One submesh of `count` vertices welded to `bone`, laid on a circle so the box holding them is
	 * wider than they are and its swept corner sits below every one of them.
	 */
	void
	AppendRing(BMesh& mesh, const uint32_t bone, const uint32_t count, const float height)
	{
		auto submesh                  = Submesh();
		submesh.layout.attributeCount = 3;
		submesh.layout.attributes[0]  = { VertexSemantic::kPosition, VertexFormat::kFloat32x3, 0 };
		submesh.layout.attributes[1]  = { VertexSemantic::kJoints0, VertexFormat::kUint16x4, 12 };
		submesh.layout.attributes[2]  = { VertexSemantic::kWeights0, VertexFormat::kUnorm16x4, 20 };
		submesh.layout.stride         = 28;
		submesh.vertexByteOffset      = static_cast<uint32_t>(mesh.vertexData.size());

		for (uint32_t i = 0; i < count; ++i)
		{
			const auto angle =
				glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(count);

			const auto position = glm::vec3(std::cos(angle), height + 0.5f * std::sin(angle), 0.0f);

			const std::array<uint16_t, 4> joints{ { static_cast<uint16_t>(bone), 0, 0, 0 } };
			const std::array<uint16_t, 4> weights{ { 65535, 0, 0, 0 } };

			const size_t base = mesh.vertexData.size();
			mesh.vertexData.resize(base + submesh.layout.stride);

			std::byte* at = mesh.vertexData.data() + base;
			std::memcpy(at, &position, sizeof(position));
			std::memcpy(at + 12, joints.data(), sizeof(joints));
			std::memcpy(at + 20, weights.data(), sizeof(weights));
			++submesh.vertexCount;
		}

		submesh.aabbMin = glm::vec3(-1.0f, height - 0.5f, 0.0f);
		submesh.aabbMax = glm::vec3(1.0f, height + 0.5f, 0.0f);
		mesh.submeshes.push_back(submesh);
	}

	/** One entry: a small ring on the foot, then `pad` vertices on the bone `padHeight` above it. */
	BMesh
	MakeFloorMesh(const Skeleton& skeleton, const uint32_t pad, const float padHeight)
	{
		auto mesh              = BMesh();
		mesh.skeleton          = "Derived/Skeletons/rig.bskel";
		mesh.skeletonSignature = skeletonSignature(skeleton);

		AppendRing(mesh, 1, 64, 0.0f);
		AppendRing(mesh, 2, pad, padHeight);

		mesh.meshes.push_back({ .firstSubmesh = 0, .submeshCount = 2, .nameOffset = 0 });
		return mesh;
	}

	/** A scratch data root holding one rig, one mesh, and `clipSets` clip sets against that rig. */
	struct RigProject
	{
		fs::path path;

		RigProject(const char* name, const uint32_t clipSets) :
			path(fs::temp_directory_path() / name)
		{
			fs::remove_all(path);
			fs::create_directories(path / "Derived/Meshes");
			fs::create_directories(path / "Derived/Skeletons");
			fs::create_directories(path / "Derived/Animations");

			const Skeleton skeleton = MakeRig(8);
			StoreAt(path).Save(skeleton, "Derived/Skeletons/rig.bskel");
			StoreAt(path).Save(MakeMesh(2, skeleton), "Derived/Meshes/rig.bmesh");

			for (uint32_t i = 0; i < clipSets; ++i)
				SaveAt(
					MakeClips(skeleton, 4),
					path / std::format("Derived/Animations/clips{}.banim", i));
		}

		~RigProject() { fs::remove_all(path); }

		RigProject(const RigProject&) = delete;
		RigProject&
		operator=(const RigProject&) = delete;
	};
}

// rebake_bounds.cpp keeps its meshes and rigs for the whole run precisely so this holds: a project
// whose rig is consulted by every clip set must not re-read megabytes of vertex data per `.banim`.
// Nothing about the containers it writes would show that guarantee being lost, so the reads are what
// the case looks at.
TEST_CASE("A rebake reads a rig's mesh once however many clip sets name it", "[perf][bounds]")
{
	const auto readsForClipSets = [](const char* name, const uint32_t clipSets) {
		const RigProject project(name, clipSets);

		const auto loose    = MountAt(project.path);
		const auto counting = std::make_shared<CountingFileSystem>(loose);

		(void)AssetStore(project.path, counting).RebakePosedBounds(false);
		return counting->ReadsOf("Derived/Meshes/rig.bmesh");
	};

	const uint32_t one  = readsForClipSets("bernini_perf_rebake_1", 1);
	const uint32_t many = readsForClipSets("bernini_perf_rebake_8", 8);

	CHECK(one > 0);  // A run that read nothing would pass the real assertion vacuously.
	CHECK(many == one);
}

// docs/skinning.md: "All entries share one walk of the clip set, so a rig drawn as 27 meshes
// evaluates each pose once." posedBounds is the same measurement for a single entry, so calling it
// per entry re-walks every pose -- which is the shape this must never regress to.
TEST_CASE("Every mesh entry shares one walk of the clip set", "[perf][bounds]")
{
	constexpr uint32_t c_Entries = 12;

	const Skeleton     skeleton   = MakeRig(c_Bones);
	const AnimationSet animations = MakeClips(skeleton, c_Frames);
	const BMesh        mesh       = MakeMesh(c_Entries, skeleton);
	const BMesh        one        = MakeMesh(1, skeleton);

	const double shared = FastestMillis(3, [&] {
		auto clips = animations;
		bakePosedBounds(clips, mesh, skeleton);
	});

	const double perEntry = FastestMillis(3, [&] {
		for (uint32_t entry = 0; entry < c_Entries; ++entry)
			(void)posedBounds(one, 0, skeleton, animations);
	});

	INFO("shared " << shared << " ms, per-entry " << perEntry << " ms");

	// Generous by design: the shape being caught is a walk repeated 12 times, so anything near
	// parity is already the defect. A machine slow enough to blur 3x would blur any ceiling too.
	CHECK(perEntry > shared * 3.0);
}

// skinning.h: a clip floor skins "only the vertices whose own bones reach below the best floor so
// far". Both meshes here carry the same vertices over the same frames; only the height of the bone
// most of them hang from differs. Raising it out of contention must therefore cost less, and before
// the per-vertex gate existed the two were the same walk and measured alike.
TEST_CASE("A clip floor does not skin the vertices that cannot be lowest", "[perf][grounding]")
{
	constexpr uint32_t c_PadVertices    = 4096;
	constexpr uint32_t c_StandingFrames = 64;

	const auto millisFor = [](const float padHeight) {
		const Skeleton     skeleton   = MakeFloorRig(padHeight);
		const AnimationSet animations = MakeStandingClip(skeleton, c_StandingFrames);
		const BMesh        mesh       = MakeFloorMesh(skeleton, c_PadVertices, padHeight);

		return FastestMillis(3, [&] {
			(void)measureClipFloors(animations, std::span<const BMesh>(&mesh, 1), skeleton);
		});
	};

	// The floor itself must not move: the gate skips a vertex only where the same convexity bound
	// the frame prune rests on proves it cannot lower the answer.
	const Skeleton     high     = MakeFloorRig(10.0f);
	const AnimationSet standing = MakeStandingClip(high, c_StandingFrames);
	const BMesh        padded   = MakeFloorMesh(high, c_PadVertices, 10.0f);
	const BMesh        unpadded = MakeFloorMesh(high, 0, 10.0f);
	const float        withPad  = measureClipFloors(standing, std::span(&padded, 1), high).front();
	const float withNone = measureClipFloors(standing, std::span(&unpadded, 1), high).front();
	CHECK(withPad == Catch::Approx(withNone));

	const double contending = millisFor(0.0f);
	const double raised     = millisFor(10.0f);

	INFO("padding at the floor " << contending << " ms, padding overhead " << raised << " ms");

	// Generous by design: the shape being caught is a gate that stopped gating, which reads as
	// parity. A machine slow enough to blur 2x would blur any ceiling too.
	CHECK(contending > raised * 2.0);
}
