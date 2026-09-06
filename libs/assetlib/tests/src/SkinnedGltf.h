#pragma once
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace assetlib::test
{
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
		std::filesystem::path dir;
		std::filesystem::path gltf;

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
			dir(std::filesystem::temp_directory_path() / name)
		{
			std::filesystem::remove_all(dir);
			std::filesystem::create_directories(dir);
			gltf = dir / "rig.gltf";

			WriteBuffer();
			WriteDocument(edits);
		}

		~SkinnedGltf() { std::filesystem::remove_all(dir); }

		/**
		 * The same scene as one self-contained `.glb` beside the `.gltf` -- what an import copies
		 * into a project, and so the only form a regeneration ever parses.
		 */
		[[nodiscard]] std::filesystem::path
		PackGlb() const
		{
			std::string json = ReadWhole(gltf);

			// A GLB carries its buffer as the binary chunk, which is buffer 0 with no uri.
			const std::string_view uri = ", \"uri\": \"rig.bin\"";
			const size_t           at  = json.find(uri);
			REQUIRE(at != std::string::npos);
			json.erase(at, uri.size());
			json.resize((json.size() + 3) / 4 * 4, ' ');

			std::string bin = ReadWhole(dir / "rig.bin");
			bin.resize((bin.size() + 3) / 4 * 4, '\0');

			const std::filesystem::path glb = dir / "rig.glb";
			std::ofstream               out(glb, std::ios::binary);
			const auto                  u32 = [&](uint32_t value) {
				out.write(reinterpret_cast<const char*>(&value), sizeof(value));
			};
			u32(0x46546C67u);  // "glTF"
			u32(2);
			u32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
			u32(static_cast<uint32_t>(json.size()));
			u32(0x4E4F534Au);  // "JSON"
			out << json;
			u32(static_cast<uint32_t>(bin.size()));
			u32(0x004E4942u);  // "BIN\0"
			out << bin;
			return glb;
		}

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
		[[nodiscard]] static std::string
		ReadWhole(const std::filesystem::path& path)
		{
			std::ifstream     in(path, std::ios::binary);
			std::stringstream buffer;
			buffer << in.rdbuf();
			return std::move(buffer).str();
		}

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

			// Slot 0 (node 2) carries a recognizable translation; slot 1 (node 1) is identity.
			// Column-major, as glTF stores them.
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
}
