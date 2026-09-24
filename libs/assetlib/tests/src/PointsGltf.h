#pragma once
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <core/math.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <system_error>
#include <vector>

// Builds `.glb` files with POINTS primitives for the grass tests: a document and one binary buffer,
// written as a GLB with a JSON and a BIN chunk, so each case writes exactly the points it needs.

namespace assetlib::test
{
	inline constexpr uint32_t c_ArrayBuffer  = 34962;
	inline constexpr uint32_t c_Float        = 5126;
	inline constexpr uint32_t c_UnsignedByte = 5121;

	/** A `.glb` built from a document and one binary buffer, removed when it goes out of scope. */
	class Glb
	{
	public:
		Glb(const char* name, nlohmann::json document, std::vector<std::byte> bin) :
			m_Path(std::filesystem::temp_directory_path() / name)
		{
			bin.resize(core::align(bin.size(), 4), std::byte{ 0 });
			document["buffers"] = nlohmann::json::array({ { { "byteLength", bin.size() } } });
			document["asset"]   = { { "version", "2.0" } };

			std::string json = document.dump();
			json.resize(core::align(json.size(), 4), ' ');

			std::ofstream out(m_Path, std::ios::binary);
			REQUIRE(out.is_open());
			const auto u32 = [&out](const uint32_t value) {
				out.write(reinterpret_cast<const char*>(&value), sizeof(value));
			};
			u32(0x46546c67);  // "glTF"
			u32(2);
			u32(static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
			u32(static_cast<uint32_t>(json.size()));
			u32(0x4e4f534a);  // "JSON"
			out.write(json.data(), static_cast<std::streamsize>(json.size()));
			u32(static_cast<uint32_t>(bin.size()));
			u32(0x004e4942);  // "BIN"
			out.write(
				reinterpret_cast<const char*>(bin.data()),
				static_cast<std::streamsize>(bin.size()));
			REQUIRE(out.good());
		}

		Glb(const Glb&) = delete;
		Glb&
		operator=(const Glb&) = delete;

		~Glb()
		{
			std::error_code error;
			std::filesystem::remove(m_Path, error);
		}

		[[nodiscard]] const std::filesystem::path&
		Path() const noexcept
		{
			return m_Path;
		}

	private:
		std::filesystem::path m_Path;
	};

	/** Accessors appended to one buffer, each 4-byte aligned. */
	class Buffer
	{
	public:
		template <typename T>
		uint32_t
		Add(const std::vector<T>& values,
		    const char*           type,
		    const uint32_t        componentType,
		    const bool            normalized = false)
		{
			const size_t offset = bytes.size();
			bytes.resize(offset + values.size() * sizeof(T));
			std::memcpy(bytes.data() + offset, values.data(), values.size() * sizeof(T));
			bytes.resize(core::align(bytes.size(), 4), std::byte{ 0 });

			const auto view = static_cast<uint32_t>(views.size());
			views.push_back(
				{ { "buffer", 0 },
			      { "byteOffset", offset },
			      { "byteLength", values.size() * sizeof(T) },
			      { "target", c_ArrayBuffer } });

			auto accessor = nlohmann::json{ { "bufferView", view },
				                            { "componentType", componentType },
				                            { "count", values.size() },
				                            { "type", type } };
			if (normalized)
				accessor["normalized"] = true;
			accessors.push_back(accessor);
			return static_cast<uint32_t>(accessors.size() - 1);
		}

		std::vector<std::byte> bytes;
		nlohmann::json         views     = nlohmann::json::array();
		nlohmann::json         accessors = nlohmann::json::array();
	};

	/** A mesh named `Street` of one triangle and one POINTS primitive over `points`. */
	inline nlohmann::json
	StreetDocument(
		Buffer&                       buffer,
		const std::vector<glm::vec3>& points,
		nlohmann::json                pointAttributes = nlohmann::json::object())
	{
		const std::vector<glm::vec3> triangle         = { glm::vec3(0, 0, 0),
			                                              glm::vec3(1, 0, 0),
			                                              glm::vec3(0, 0, 1) };
		const uint32_t               triangleAccessor = buffer.Add(triangle, "VEC3", c_Float);
		pointAttributes["POSITION"]                   = buffer.Add(points, "VEC3", c_Float);

		auto document      = nlohmann::json::object();
		document["scene"]  = 0;
		document["scenes"] = { { { "nodes", { 0 } } } };
		document["nodes"]  = { { { "mesh", 0 }, { "name", "Street" } } };
		document["meshes"] = { { { "name", "Street" },
			                     { "primitives",
			                       { { { "attributes", { { "POSITION", triangleAccessor } } } },
			                         { { "attributes", pointAttributes }, { "mode", 0 } } } } } };
		document["bufferViews"] = buffer.views;
		document["accessors"]   = buffer.accessors;
		return document;
	}

	/** Points on a `side` x `side` grid one metre apart, in a fixed shuffled order. */
	inline std::vector<glm::vec3>
	ShuffledGrid(const uint32_t side)
	{
		auto points = std::vector<glm::vec3>();
		for (uint32_t z = 0; z < side; ++z)
			for (uint32_t x = 0; x < side; ++x)
				points.emplace_back(static_cast<float>(x), 0.0f, static_cast<float>(z));

		std::mt19937 shuffle(7);
		std::ranges::shuffle(points, shuffle);
		return points;
	}
}
