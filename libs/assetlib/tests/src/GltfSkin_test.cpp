#include <assetlib/asset_import.h>
#include <assetlib/banim_io.h>
#include <assetlib/bmesh_gltf.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib/skeleton.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <assetlib_structs/Skeleton.h>

#include "mounted_io.h"
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace assetlib;
using namespace assetlib::imp;

namespace
{
	namespace fs = std::filesystem;

	/**
	 * A skinned, animated glTF written to a temp directory, with its buffer as a sibling `.bin` --
	 * hand-rolling base64 for a document this size buys nothing.
	 *
	 * The rig is deliberately awkward in the two ways a real export is:
	 *
	 * - `skin.joints` is [node 2, node 1], and node 2 is node 1's *child*. So bone order is not joint
	 *   order, which is what the joint remap and the inverse-bind reorder have to survive.
	 * - the two joints carry different inverse bind matrices, so a reorder that missed them is visible.
	 */
	struct SkinnedGltf
	{
		fs::path dir;
		fs::path gltf;

		/** A substring of the document and what to put in its place. */
		struct Edit
		{
			std::string_view find;
			std::string_view replace;
		};

		/**
		 * @param edits Applied in order before the document is written, so a case can doctor one
		 *        accessor -- or graft a node above the rig -- without restating the whole file.
		 */
		explicit SkinnedGltf(const char* name, std::initializer_list<Edit> edits = {}) :
			dir(fs::temp_directory_path() / name)
		{
			fs::remove_all(dir);
			fs::create_directories(dir);
			gltf = dir / "rig.gltf";

			WriteBuffer();
			WriteDocument(edits);
		}

		~SkinnedGltf() { fs::remove_all(dir); }

		// Offsets into the buffer, in the order it is laid out.
		static constexpr size_t c_Positions   = 0;    // 3 x vec3 float
		static constexpr size_t c_Joints      = 36;   // 3 x u8 vec4
		static constexpr size_t c_Weights     = 48;   // 3 x vec4 float
		static constexpr size_t c_Indices     = 96;   // 3 x u16
		static constexpr size_t c_InverseBind = 104;  // 2 x mat4 float
		static constexpr size_t c_MoveTimes   = 232;  // 2 x float
		static constexpr size_t c_MoveValues  = 240;  // 2 x vec3 float
		static constexpr size_t c_SpinTimes   = 264;  // 3 x float
		static constexpr size_t c_SpinValues  = 276;  // 3 x vec4 float
		static constexpr size_t c_UnitScale   = 324;  // 2 x vec3 float
		static constexpr size_t c_Length      = 348;

	private:
		void
		WriteBuffer() const
		{
			std::vector<std::byte> bytes(c_Length, std::byte{ 0 });

			const auto put = [&](size_t offset, const auto& values) {
				std::memcpy(
					bytes.data() + offset,
					values.data(),
					values.size() * sizeof(values[0]));
			};

			put(c_Positions,
			    std::array<float, 9>{ { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f } });

			// Joint *slots*, not bone indices: slot 0 is node 2 and slot 1 is node 1.
			put(c_Joints, std::array<uint8_t, 12>{ { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 } });

			put(c_Weights,
			    std::array<float, 12>{
					{ 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.25f, 0.75f, 0.0f, 0.0f } });

			put(c_Indices, std::array<uint16_t, 3>{ { 0, 1, 2 } });

			// Slot 0 (node 2) carries a recognizable translation; slot 1 (node 1) is identity. Column-
			// major, as glTF stores them.
			std::array<float, 32> inverseBind{};
			inverseBind[0] = inverseBind[5] = inverseBind[10] = inverseBind[15] = 1.0f;
			inverseBind[12]                                                     = 7.0f;
			inverseBind[13]                                                     = 8.0f;
			inverseBind[14]                                                     = 9.0f;
			inverseBind[16] = inverseBind[21] = inverseBind[26] = inverseBind[31] = 1.0f;
			put(c_InverseBind, inverseBind);

			put(c_MoveTimes, std::array<float, 2>{ { 0.0f, 1.0f } });
			put(c_MoveValues, std::array<float, 6>{ { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f } });

			put(c_SpinTimes, std::array<float, 3>{ { 0.0f, 0.5f, 1.0f } });
			put(c_SpinValues,
			    std::array<float, 12>{ { 0.0f,
			                             0.0f,
			                             0.0f,
			                             1.0f,
			                             0.0f,
			                             0.7071068f,
			                             0.0f,
			                             0.7071068f,
			                             0.0f,
			                             0.0f,
			                             0.0f,
			                             1.0f } });

			// A scale track pinned at 1. Redundant on its own, and every exporter that puts a unit
			// conversion on an armature node above the rig writes one anyway -- which is what makes
			// it the thing that overwrites a composed bind pose.
			put(c_UnitScale, std::array<float, 6>{ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } });

			std::ofstream out(dir / "rig.bin", std::ios::binary);
			out.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
		}

		void
		WriteDocument(std::initializer_list<Edit> edits) const
		{
			std::string document = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [
    { "mesh": 0, "skin": 0, "name": "body" },
    { "name": "hips", "translation": [ 0, 1, 0 ], "children": [ 2 ] },
    { "name": "spine", "translation": [ 0, 2, 0 ] }
  ],
  "skins": [ { "joints": [ 2, 1 ], "inverseBindMatrices": 4 } ],
  "meshes": [ { "name": "body", "primitives": [ {
    "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
    "indices": 3, "mode": 4 } ] } ],
  "animations": [
    { "name": "walk",
      "samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ] },
    { "name": "spin",
      "samplers": [ { "input": 7, "output": 8, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ] }
  ],
  "buffers": [ { "byteLength": 348, "uri": "rig.bin" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 12 },
    { "buffer": 0, "byteOffset": 48,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 96,  "byteLength": 6 },
    { "buffer": 0, "byteOffset": 104, "byteLength": 128 },
    { "buffer": 0, "byteOffset": 232, "byteLength": 8 },
    { "buffer": 0, "byteOffset": 240, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 264, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 276, "byteLength": 48 },
    { "buffer": 0, "byteOffset": 324, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR",
      "min": [ 0 ], "max": [ 1 ] },
    { "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3" },
    { "bufferView": 7, "componentType": 5126, "count": 3, "type": "SCALAR",
      "min": [ 0 ], "max": [ 1 ] },
    { "bufferView": 8, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 9, "componentType": 5126, "count": 2, "type": "VEC3" }
  ]
})";

			for (const Edit& edit : edits)
			{
				const size_t at = document.find(edit.find);
				REQUIRE(at != std::string::npos);
				document.replace(at, edit.find.size(), edit.replace);
			}

			std::ofstream out(gltf, std::ios::binary);
			out << document;
		}
	};

	uint16_t
	U16At(const std::vector<std::byte>& data, size_t offset)
	{
		uint16_t value = 0;
		std::memcpy(&value, data.data() + offset, sizeof(value));
		return value;
	}
}

TEST_CASE("A glTF skin arrives topologically sorted, not in joint order", "[gltf][skeleton]")
{
	const SkinnedGltf source("bernini_gltf_skin_sorted");
	const auto        import = loadFromGltf(source.gltf);

	REQUIRE(import.skeleton.bones.size() == 2);

	// skin.joints is [spine, hips] and spine is hips' child, so the sort has to invert it.
	CHECK(import.skeleton.stringPool.at(import.skeleton.bones[0].nameOffset) == "hips");
	CHECK(import.skeleton.stringPool.at(import.skeleton.bones[1].nameOffset) == "spine");
	CHECK(import.skeleton.bones[0].parent == c_InvalidIndex);
	CHECK(import.skeleton.bones[1].parent == 0);

	// Each bone's bind pose is local to its bone parent, which here is also its node parent.
	CHECK(import.skeleton.bones[0].bindPose.translation.y == Catch::Approx(1.0f));
	CHECK(import.skeleton.bones[1].bindPose.translation.y == Catch::Approx(2.0f));

	// The inverse binds must have travelled with their joints. The recognizable one belongs to spine,
	// which the sort moved from index 0 to index 1.
	CHECK(import.skeleton.bones[1].inverseBind[3] == glm::vec4(7.0f, 8.0f, 9.0f, 1.0f));
	CHECK(import.skeleton.bones[0].inverseBind == glm::mat4(1.0f));
}

TEST_CASE("A skinned primitive's joint indices are remapped into bone order", "[gltf][skeleton]")
{
	// This is the failure the reordering exists to avoid, and the one nothing downstream can see: a
	// JOINTS_0 left in the file's joint order names the wrong bone and skins the mesh to it silently.
	const SkinnedGltf source("bernini_gltf_skin_joints");
	const auto        import = loadFromGltf(source.gltf);

	REQUIRE(import.submeshes.size() == 1);
	const Submesh& submesh = import.submeshes.front();

	REQUIRE(submesh.layout.attributeCount == 3);
	CHECK(submesh.layout.stride == 28);  // vec3 position + u16x4 joints + unorm16x4 weights

	const auto joints  = attributeOffset(submesh.layout, VertexSemantic::kJoints0);
	const auto weights = attributeOffset(submesh.layout, VertexSemantic::kWeights0);
	REQUIRE(joints);
	REQUIRE(weights);
	REQUIRE(joints >= 0);
	REQUIRE(weights >= 0);

	const auto jointAt = [&](uint32_t vertex, uint32_t component) {
		return U16At(
			import.vertexData,
			static_cast<size_t>(vertex) * submesh.layout.stride + static_cast<size_t>(*joints) +
				component * sizeof(uint16_t));
	};

	// Slot 0 is spine, which sorted to bone 1; slot 1 is hips, which sorted to bone 0.
	CHECK(jointAt(0, 0) == 1);
	CHECK(jointAt(1, 0) == 0);
	CHECK(jointAt(2, 0) == 1);
	CHECK(jointAt(2, 1) == 0);

	const auto weightAt = [&](uint32_t vertex, uint32_t component) {
		return U16At(
			import.vertexData,
			static_cast<size_t>(vertex) * submesh.layout.stride + static_cast<size_t>(*weights) +
				component * sizeof(uint16_t));
	};

	CHECK(weightAt(0, 0) == 65535);
	CHECK(weightAt(1, 0) == 32768);
	CHECK(weightAt(1, 1) == 32768);
	CHECK(weightAt(2, 0) == 16384);
	CHECK(weightAt(2, 1) == 49151);
}

TEST_CASE("A static glTF imports with no rig and no skin attributes", "[gltf][skeleton]")
{
	// The skin is what says which nodes are bones, so a file without one is not a rig with an empty
	// skeleton -- it is a static mesh, and its layout must not grow.
	const fs::path glb = "assets/suzanne.glb";
	REQUIRE(fs::exists(glb));

	const auto import = loadFromGltf(glb);

	CHECK(import.skeleton.bones.empty());
	CHECK(import.animations.clips.empty());
	for (const Submesh& submesh : import.submeshes)
	{
		CHECK_FALSE(attributeOffset(submesh.layout, VertexSemantic::kJoints0).has_value());
		CHECK_FALSE(attributeOffset(submesh.layout, VertexSemantic::kWeights0).has_value());
	}
}

TEST_CASE("A glTF's animations are resampled to a fixed rate", "[gltf][animation]")
{
	const SkinnedGltf source("bernini_gltf_skin_clips");
	const auto        import = loadFromGltf(source.gltf);

	REQUIRE(import.animations.clips.size() == 2);
	CHECK(import.animations.boneCount == 2);
	CHECK(import.animations.skeletonSignature == skeletonSignature(import.skeleton));

	const AnimationClip& walk = import.animations.clips[0];
	CHECK(import.animations.stringPool.at(walk.nameOffset) == "walk");
	CHECK(walk.sampleRate == Catch::Approx(c_DefaultSampleRate));
	CHECK(walk.duration == Catch::Approx(1.0f));

	// [0, duration] inclusive: a one-second clip at 30 Hz is 31 poses, not 30.
	CHECK(walk.frameCount == 31);

	const auto poseOf = [&](const AnimationClip& clip, uint32_t frame, uint32_t bone) {
		return import.animations
		    .samples[clip.firstSample + frame * import.animations.boneCount + bone];
	};

	// The channel is linear from (0,0,0) to (0,0,2), so the middle frame lands exactly halfway.
	CHECK(poseOf(walk, 0, 0).translation.z == Catch::Approx(0.0f));
	CHECK(poseOf(walk, 15, 0).translation.z == Catch::Approx(1.0f));
	CHECK(poseOf(walk, 30, 0).translation.z == Catch::Approx(2.0f));

	// A bone no channel targets holds its bind pose rather than collapsing to the origin.
	CHECK(poseOf(walk, 15, 1).translation.y == Catch::Approx(2.0f));

	SECTION("and a different rate changes the frame count, not the length")
	{
		const auto faster = loadFromGltf(source.gltf, {}, 60.0f);
		REQUIRE(faster.animations.clips.size() == 2);
		CHECK(faster.animations.clips[0].frameCount == 61);
		CHECK(faster.animations.clips[0].duration == Catch::Approx(1.0f));
	}
}

TEST_CASE("A clip's root motion and loop flag come from its own samples", "[gltf][animation]")
{
	const SkinnedGltf source("bernini_gltf_skin_meta");
	const auto        import = loadFromGltf(source.gltf);

	REQUIRE(import.animations.clips.size() == 2);
	const AnimationClip& walk = import.animations.clips[0];
	const AnimationClip& spin = import.animations.clips[1];

	// Two metres of +Z over one second. Horizontal, so it is also the authored locomotion speed.
	CHECK(walk.rootMotion.z == Catch::Approx(2.0f));
	CHECK(walk.locomotionSpeed == Catch::Approx(2.0f));

	// glTF has no loop flag, so the evidence is the data: `walk` ends two metres from where it began,
	// and `spin` returns to the pose it started from.
	CHECK(walk.loop == 0);
	CHECK(spin.loop == 1);

	// `spin` rotates the root without moving it, so it is not locomotion.
	CHECK(spin.rootMotion == glm::vec3(0.0f));
	CHECK(spin.locomotionSpeed == Catch::Approx(0.0f));
}

namespace
{
	// The scene as the fixture writes it -- both joints at the top level. What an armature case
	// replaces, to hang the rig off a node above it instead.
	constexpr std::string_view c_JointsAtTopLevel = R"("scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [
    { "mesh": 0, "skin": 0, "name": "body" },
    { "name": "hips", "translation": [ 0, 1, 0 ], "children": [ 2 ] },
    { "name": "spine", "translation": [ 0, 2, 0 ] }
  ],)";
}

TEST_CASE("A node above the root joint poses the rig, not just its bind pose", "[gltf][animation]")
{
	// The bug this pins cost the whole animal library: an exporter puts the rig's unit conversion on
	// an armature node above the root joint, and writes a redundant unit-scale track on the joint
	// itself. Folding the armature into the bind pose alone is not enough -- the first frame of any
	// clip overwrites that scale with the joint's own 1, and the rig poses a hundred times too large.
	const SkinnedGltf source(
		"bernini_gltf_armature_scale",
		{ { c_JointsAtTopLevel,
	        R"("scenes": [ { "nodes": [ 0, 3 ] } ],
  "nodes": [
    { "mesh": 0, "skin": 0, "name": "body" },
    { "name": "hips", "translation": [ 0, 1, 0 ], "children": [ 2 ] },
    { "name": "spine", "translation": [ 0, 2, 0 ] },
    { "name": "armature", "translation": [ 0, 0, 5 ], "scale": [ 0.01, 0.01, 0.01 ],
      "children": [ 1 ] }
  ],)" },
	      { R"("samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ] },)",
	        R"("samplers": [ { "input": 5, "output": 6, "interpolation": "LINEAR" },
                     { "input": 5, "output": 9, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } },
                    { "sampler": 1, "target": { "node": 1, "path": "scale" } } ] },)" } });

	const auto import = loadFromGltf(source.gltf);

	REQUIRE(import.skeleton.bones.size() == 2);
	REQUIRE(import.animations.clips.size() == 2);

	// Catch has no printer for a glm::vec3, so a scale is checked through its extremes -- which also
	// says it stayed uniform, and prints a number when it did not.
	const auto checkScale = [](const glm::vec3& scale, float expected) {
		REQUIRE(std::max({ scale.x, scale.y, scale.z }) == Catch::Approx(expected));
		REQUIRE(std::min({ scale.x, scale.y, scale.z }) == Catch::Approx(expected));
	};

	// The bind pose has always composed the armature in.
	checkScale(import.skeleton.bones[0].bindPose.scale, 0.01f);
	CHECK(import.skeleton.bones[0].bindPose.translation.z == Catch::Approx(5.0f));

	const AnimationClip& walk = import.animations.clips[0];

	const auto poseOf = [&](uint32_t frame) {
		return import.animations.samples[walk.firstSample + frame * import.animations.boneCount];
	};

	// Every frame, not just the first: the joint's own unit-scale track must lose to the armature's
	// conversion at all times, or the rig grows the moment a clip starts.
	for (uint32_t frame = 0; frame < walk.frameCount; ++frame)
		checkScale(poseOf(frame).scale, 0.01f);

	// The joint travels 2 along +Z, under an armature that shrinks it by a hundred and stands at 5.
	CHECK(poseOf(0).translation.z == Catch::Approx(5.0f));
	CHECK(poseOf(walk.frameCount - 1).translation.z == Catch::Approx(5.02f));

	// Which is what the clip travels. Composing nothing would report the joint's raw 2.
	CHECK(walk.rootMotion.z == Catch::Approx(0.02f));
}

TEST_CASE("A clip's root motion may be authored above the root joint", "[gltf][animation]")
{
	// The same armature, animated. A channel on it is the clip's travel -- drop it and every clip of
	// a rig built this way reports itself as an in-place cycle going nowhere.
	const SkinnedGltf source(
		"bernini_gltf_armature_motion",
		{ { c_JointsAtTopLevel,
	        R"("scenes": [ { "nodes": [ 0, 3 ] } ],
  "nodes": [
    { "mesh": 0, "skin": 0, "name": "body" },
    { "name": "hips", "translation": [ 0, 1, 0 ], "children": [ 2 ] },
    { "name": "spine", "translation": [ 0, 2, 0 ] },
    { "name": "armature", "children": [ 1 ] }
  ],)" },
	      { R"("channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } } ] },)",
	        R"("channels": [ { "sampler": 0, "target": { "node": 1, "path": "translation" } },
                    { "sampler": 0, "target": { "node": 3, "path": "translation" } } ] },)" } });

	const auto import = loadFromGltf(source.gltf);

	REQUIRE(import.animations.clips.size() == 2);
	const AnimationClip& walk = import.animations.clips[0];

	// Two metres on the armature and two more on the joint beneath it, at unit scale.
	CHECK(walk.rootMotion.z == Catch::Approx(4.0f));
	CHECK(walk.locomotionSpeed == Catch::Approx(4.0f));
}

TEST_CASE("A rigid mesh parented to a joint rides that bone", "[gltf][skeleton][animation]")
{
	// Eyes, teeth and props are modelled this way: an unskinned mesh parented to a bone, which in
	// every DCC means "follow it". Nothing at runtime knows about parenting, so the import binds such
	// a mesh to that bone at full weight -- which is what the parenting already meant.
	const SkinnedGltf source(
		"bernini_gltf_attachment",
		{ { c_JointsAtTopLevel,
	        R"("scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [
    { "mesh": 0, "skin": 0, "name": "body" },
    { "name": "hips", "translation": [ 0, 1, 0 ], "children": [ 2, 3 ] },
    { "name": "spine", "translation": [ 0, 2, 0 ] },
    { "name": "eye", "mesh": 1, "translation": [ 0, 0, 3 ] }
  ],)" },
	      { R"("meshes": [ { "name": "body", "primitives": [ {
    "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
    "indices": 3, "mode": 4 } ] } ],)",
	        R"("meshes": [ { "name": "body", "primitives": [ {
    "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 },
    "indices": 3, "mode": 4 } ] },
    { "name": "eye", "primitives": [ {
    "attributes": { "POSITION": 0 }, "indices": 3, "mode": 4 } ] } ],)" } });

	const auto import = loadFromGltf(source.gltf);

	REQUIRE(import.meshes.size() == 2);
	REQUIRE(import.skeleton.bones.size() == 2);

	const Submesh& eye = import.submeshes[import.meshes[1].firstSubmesh];

	// hips is bone 0 -- and the eye carries a binding it was never authored with.
	const auto joints  = attributeOffset(eye.layout, VertexSemantic::kJoints0);
	const auto weights = attributeOffset(eye.layout, VertexSemantic::kWeights0);
	REQUIRE(joints);
	REQUIRE(weights);

	const auto at = [&](std::optional<int> offset, uint32_t vertex, uint32_t component) {
		return U16At(
			import.vertexData,
			eye.vertexByteOffset + static_cast<size_t>(vertex) * eye.layout.stride +
				static_cast<size_t>(*offset) + component * sizeof(uint16_t));
	};

	for (uint32_t vertex = 0; vertex < eye.vertexCount; ++vertex)
	{
		REQUIRE(at(joints, vertex, 0) == 0);
		REQUIRE(at(weights, vertex, 0) == std::numeric_limits<uint16_t>::max());
		REQUIRE(at(weights, vertex, 1) == 0);  // the other three influences skin nothing
	}

	// Full weight on a bone undoes that bone's inverse bind, so the vertices have to arrive in the
	// rig's space rather than the node's: the eye's own origin sits at hips + (0,0,3).
	const auto position = [&](uint32_t vertex) {
		glm::vec3 value;
		std::memcpy(
			&value,
			import.vertexData.data() + eye.vertexByteOffset +
				static_cast<size_t>(vertex) * eye.layout.stride,
			sizeof(value));
		return value;
	};
	CHECK(position(0) == glm::vec3(0.0f, 1.0f, 3.0f));

	SECTION("and travels with it once a clip plays")
	{
		// The whole point, and what a bind-pose placement cannot do. `walk` slides hips two along +Z,
		// so every vertex of a mesh hanging off hips must move exactly that far -- through the same
		// CPU skinning path the GPU is measured against.
		const BMesh        mesh  = toBMesh(import);
		const Skeleton&    rig   = import.skeleton;
		const AnimationSet clips = import.animations;

		const AnimationClip& walk = clips.clips[0];

		const auto skinnedAt = [&](uint32_t frame) {
			return skinSubmesh(
				mesh,
				mesh.submeshes[mesh.meshes[1].firstSubmesh],
				skinningMatrices(rig, poseModelTransforms(rig, clips, 0, frame)));
		};

		const auto first = skinnedAt(0);
		const auto last  = skinnedAt(walk.frameCount - 1);
		REQUIRE(first.size() == eye.vertexCount);

		for (size_t vertex = 0; vertex < first.size(); ++vertex)
		{
			const glm::vec3 travelled = last[vertex].position - first[vertex].position;
			CHECK(travelled.x == Catch::Approx(0.0f).margin(1e-5f));
			CHECK(travelled.y == Catch::Approx(0.0f).margin(1e-5f));
			CHECK(travelled.z == Catch::Approx(2.0f));
		}
	}
}

TEST_CASE("Importing a skinned mesh writes the rig it names", "[gltf][skeleton][io]")
{
	const SkinnedGltf source("bernini_gltf_skin_bake");
	const auto        import = loadFromGltf(source.gltf);

	const fs::path outDir = source.dir / "baked";
	fs::create_directories(outDir);

	BMesh baked = toBMesh(import);
	static_cast<void>(generateTangents(baked));
	writeImportedRig(
		import,
		baked,
		outDir,
		outDir / "rig.bskel",
		outDir / "rig.banim",
		true,
		SourceRef{});
	writeImportedMesh(baked, outDir / "rig.bmesh");

	const auto mesh = load(outDir / "rig.bmesh");

	// A mesh whose vertices carry joints and that names no skeleton has indices nothing can resolve,
	// which is why the two are written together rather than the rig being an authoring choice.
	REQUIRE(isSkinned(mesh));
	CHECK(mesh.skeleton == "rig.bskel");
	CHECK(loadMeshRefs(outDir / "rig.bmesh").skeleton == "rig.bskel");

	const auto skeleton = loadSkeleton(outDir / mesh.skeleton);
	REQUIRE(skeleton.bones.size() == 2);

	const auto animations = loadAnimations(outDir / "rig.banim");
	CHECK(animations.skeleton == "rig.bskel");
	CHECK(animations.clips.size() == 2);
	CHECK(animationsMatchSkeleton(animations, skeleton));
}

TEST_CASE("A malformed animation sampler is rejected, not read past", "[gltf][skin][animation]")
{
	// times and values come from two independent accessors, so a file is free to disagree about how
	// many of each there are. Nothing downstream rechecks, so both are caught at the read.

	SECTION("values that do not cover the keyframe times")
	{
		const SkinnedGltf source(
			"bernini_gltf_sampler_short",
			{ { R"({ "bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3" })",
		        R"({ "bufferView": 6, "componentType": 5126, "count": 1, "type": "VEC3" })" } });

		CHECK_THROWS_WITH(
			loadFromGltf(source.gltf),
			Catch::Matchers::ContainsSubstring("values to match them"));
	}

	SECTION("an output accessor wider than a TRS channel")
	{
		// evaluate returns a vec4 and fills one lane per component, so a MAT4 output would run off
		// the end of it. Accessor 4 is the rig's inverse bind matrices -- real MAT4 data.
		const SkinnedGltf source(
			"bernini_gltf_sampler_mat4",
			{ { R"("input": 5, "output": 6)", R"("input": 5, "output": 4)" } });

		CHECK_THROWS_WITH(
			loadFromGltf(source.gltf),
			Catch::Matchers::ContainsSubstring("which is not a TRS channel"));
	}
}

TEST_CASE("A mesh carrying joints must name a skeleton", "[bmesh][io][skeleton]")
{
	// A joint index is a bare number into a bone array. Detached from the skeleton it was remapped
	// into it resolves to nothing, and neither the renderer nor a reader can tell -- so the file is
	// refused at both ends rather than written and discovered later.
	const SkinnedGltf source("bernini_gltf_skin_invariant");
	const auto        import = loadFromGltf(source.gltf);

	BMesh mesh = toBMesh(import);
	REQUIRE(isSkinned(mesh));
	REQUIRE(mesh.skeleton.empty());  // toBMesh assigns no paths; bake is what names the rig

	CHECK_THROWS_AS(serialize(mesh), std::runtime_error);

	SECTION("and naming one makes it writable again")
	{
		mesh.skeleton = "rig.bskel";
		CHECK(deserialize(serialize(mesh)).skeleton == "rig.bskel");
	}

	SECTION(
		"but a mesh with no joints may still name one, which is how an attachment hangs off a bone")
	{
		BMesh attachment    = mesh;
		attachment.skeleton = "rig.bskel";
		for (Submesh& submesh : attachment.submeshes) submesh.layout.attributeCount = 1;

		REQUIRE_FALSE(isSkinned(attachment));
		CHECK(deserialize(serialize(attachment)).skeleton == "rig.bskel");
	}
}

TEST_CASE("A mesh that names no skeleton loads as a static mesh", "[bmesh][io]")
{
	// Chunks are addressed by id and an absent or empty one is not an error, so a mesh with nothing
	// in its skeleton chunk reads as what it is, a static mesh.
	const fs::path bmesh = "assets/Data/Meshes/apples.bmesh";
	REQUIRE(fs::exists(bmesh));

	const auto mesh = load(bmesh);
	CHECK_FALSE(mesh.materials.empty());
	CHECK(mesh.skeleton.empty());
	CHECK_FALSE(isSkinned(mesh));
}
