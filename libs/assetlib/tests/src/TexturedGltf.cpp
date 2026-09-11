#include "TexturedGltf.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <core/math.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

namespace assetlib::test
{
	namespace
	{
		// Two named 32x32 checkerboards preserve image routing and expose nonlinear mip averaging.
		constexpr const char* c_Document = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0, 1 ] } ],
  "nodes": [ { "mesh": 0, "name": "Apple1" }, { "mesh": 1, "name": "Apple2" } ],
  "meshes": [
    { "name": "Apple1", "primitives": [ { "attributes": { "POSITION": 0, "TEXCOORD_0": 1 }, "indices": 2, "material": 0 } ] },
    { "name": "Apple2", "primitives": [ { "attributes": { "POSITION": 0, "TEXCOORD_0": 1 }, "indices": 2, "material": 1 } ] }
  ],
  "buffers": [ { "byteLength": 66, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24, "target": 34962 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 6, "target": 34963 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [ 0, 0, 0 ], "max": [ 1, 1, 0 ] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "images": [
    { "name": "Apple1_u1_v1_diffuse", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAALklEQVR4nO3PsREAAAgCMfZfGgtX0C4UX3KXJGn72N/3HQEBAQEBAQEBAQHBRQfLs/pMvRTw+wAAAABJRU5ErkJggg==" },
    { "name": "Apple2_u1_v1_diffuse", "uri": "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAAMUlEQVR4nGNgYGD476BAQ5K2pkPAqA9GfTDqg1EfjPpg1AejPhj1wagPRn0w6gNqkACjnr4flttZawAAAABJRU5ErkJggg==" }
  ],
  "textures": [ { "source": 0 }, { "source": 1 } ],
  "materials": [
    { "name": "Apple1", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } },
    { "name": "Apple2", "pbrMetallicRoughness": { "baseColorTexture": { "index": 1 } } }
  ]
})";

		struct TexturedGltf
		{
			std::filesystem::path path =
				std::filesystem::temp_directory_path() / "bernini_textured_fixture.glb";

			TexturedGltf()
			{
				std::string json = c_Document;
				json.resize(core::align(json.size(), 4), ' ');
				std::ofstream out(path, std::ios::binary);
				REQUIRE(out.is_open());
				const auto writeU32 = [&](uint32_t value) {
					const std::array<char, 4> bytes{ { static_cast<char>(value & 0xff),
						                               static_cast<char>((value >> 8) & 0xff),
						                               static_cast<char>((value >> 16) & 0xff),
						                               static_cast<char>((value >> 24) & 0xff) } };
					out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				};
				writeU32(0x46546c67);
				writeU32(2);
				writeU32(static_cast<uint32_t>(20 + json.size()));
				writeU32(static_cast<uint32_t>(json.size()));
				writeU32(0x4e4f534a);
				out.write(json.data(), static_cast<std::streamsize>(json.size()));
				out.close();
				REQUIRE(out.good());
			}

			~TexturedGltf()
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}
		};
	}

	const std::filesystem::path&
	TexturedGltfPath()
	{
		static const TexturedGltf c_Source;
		return c_Source.path;
	}
}
