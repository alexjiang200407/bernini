#include <assetlib/bmaterial_io.h>

#include "ByteReader.h"
#include "ByteWriter.h"
#include "fs_util.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x54414D42u;  // 'B','M','A','T' little-endian

		constexpr uint16_t c_VersionMajor = 5;
		constexpr uint16_t c_VersionMinor = 0;

		// Strings are stored as a uint32 length followed by the raw bytes (no terminator).
		void
		writeString(ByteWriter& writer, const std::string& value)
		{
			writer.writePod<uint32_t>(static_cast<uint32_t>(value.size()));
			writer.writeBytes(std::as_bytes(std::span<const char>(value)));
		}

		std::string
		readString(ByteReader& reader)
		{
			const auto length = reader.readPod<uint32_t>();
			const auto bytes  = reader.readBytes(length);
			return std::string(reinterpret_cast<const char*>(bytes.data()), length);
		}
	}

	std::vector<std::byte>
	serializeMaterial(const BMaterial& material)
	{
		ByteWriter writer;
		writer.writePod(c_Magic);
		writer.writePod(c_VersionMajor);
		writer.writePod(c_VersionMinor);
		writer.writePod(static_cast<uint32_t>(material.mode));
		writer.writePod(material.baseColorFactor);
		writer.writePod(material.metallicFactor);
		writer.writePod(material.roughnessFactor);
		writeString(writer, material.name);
		writeString(writer, material.baseColorTexture);
		writeString(writer, material.normalTexture);
		writeString(writer, material.ormTexture);
		for (const ChannelRoute& route : material.routes)
		{
			writeString(writer, route.texture);
			writer.writePod(route.channel);
		}
		writeString(writer, material.editorGraph);
		for (const SourceStamp& stamp : material.routeStamps)
		{
			writer.writePod(stamp.size);
			writer.writePod(stamp.mtime);
		}
		writer.writePod(static_cast<uint32_t>(material.alphaMode));
		writer.writePod(material.alphaCutoff);
		return writer.take();
	}

	BMaterial
	deserializeMaterial(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);

		if (reader.readPod<uint32_t>() != c_Magic)
			throw std::runtime_error("bmaterial: bad magic");

		const auto versionMajor = reader.readPod<uint16_t>();
		(void)reader.readPod<uint16_t>();  // minor is forward-compatible

		if (versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"bmaterial: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_VersionMajor) + "); re-bake the material");

		BMaterial material;
		material.mode             = static_cast<MaterialMode>(reader.readPod<uint32_t>());
		material.baseColorFactor  = reader.readPod<glm::vec4>();
		material.metallicFactor   = reader.readPod<float>();
		material.roughnessFactor  = reader.readPod<float>();
		material.name             = readString(reader);
		material.baseColorTexture = readString(reader);
		material.normalTexture    = readString(reader);
		material.ormTexture       = readString(reader);

		for (ChannelRoute& route : material.routes)
		{
			route.texture = readString(reader);
			route.channel = reader.readPod<uint16_t>();
		}

		material.editorGraph = readString(reader);

		for (SourceStamp& stamp : material.routeStamps)
		{
			stamp.size  = reader.readPod<uint64_t>();
			stamp.mtime = reader.readPod<int64_t>();
		}

		material.alphaMode   = static_cast<AlphaMode>(reader.readPod<uint32_t>());
		material.alphaCutoff = reader.readPod<float>();

		return material;
	}

	void
	saveMaterial(const BMaterial& material, const std::filesystem::path& path)
	{
		const auto bytes = serializeMaterial(material);

		// Cleared so fileErrorMessage cannot blame a stale errno from an unrelated call for the failure.
		errno = 0;
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error(
				fileErrorMessage("bmaterial: cannot open file for writing", path));

		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error(fileErrorMessage("bmaterial: failed to write file", path));
	}

	BMaterial
	loadMaterial(const std::filesystem::path& path)
	{
		const auto bytes = core::file::readFileBytes(path.string());
		return deserializeMaterial(bytes);
	}

	SourceStamp
	stampOf(const std::filesystem::path& path)
	{
		std::error_code ec;

		const auto size = std::filesystem::file_size(path, ec);
		if (ec)
			return {};

		const auto written = std::filesystem::last_write_time(path, ec);
		if (ec)
			return {};

		// Seconds, not the native tick: file_time_type's resolution and epoch are implementation
		// defined, and a stamp has to survive being written on one machine and compared on another.
		const auto seconds =
			std::chrono::duration_cast<std::chrono::seconds>(written.time_since_epoch()).count();

		return SourceStamp{ static_cast<uint64_t>(size), static_cast<int64_t>(seconds) };
	}

	bool
	bakeIsStale(const BMaterial& material, const std::filesystem::path& dataRoot)
	{
		bool hasRoutes = false;

		for (size_t i = 0; i < c_LooseChannelCount; ++i)
		{
			const ChannelRoute& route = material.routes[i];
			if (route.texture.empty())
				continue;

			hasRoutes = true;

			// A zeroed stamp means this route was never baked; stampOf zeroes a missing file. Neither
			// can equal a live source's stamp, so both fall out of this comparison as stale.
			if (stampOf(dataRoot / route.texture) != material.routeStamps[i])
				return true;
		}

		// No routes: an imported, triplet-only material. It has no sources to have drifted from.
		if (!hasRoutes)
			return false;

		// Routed and every source matches -- but a bake that produced no base colour never ran.
		return material.baseColorTexture.empty();
	}
}
