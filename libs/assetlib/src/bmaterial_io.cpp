#include <assetlib/bmaterial_io.h>

#include "ByteReader.h"
#include "ByteWriter.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic        = 0x54414D42u;  // 'B','M','A','T' little-endian
		constexpr uint16_t c_VersionMajor = 1;
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
		writer.writePod(material.baseColorFactor);
		writer.writePod(material.metallicFactor);
		writer.writePod(material.roughnessFactor);
		writeString(writer, material.name);
		writeString(writer, material.baseColorTexture);
		writeString(writer, material.normalTexture);
		writeString(writer, material.ormTexture);
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
			throw std::runtime_error("bmaterial: unsupported major version");

		BMaterial material;
		material.baseColorFactor  = reader.readPod<glm::vec4>();
		material.metallicFactor   = reader.readPod<float>();
		material.roughnessFactor  = reader.readPod<float>();
		material.name             = readString(reader);
		material.baseColorTexture = readString(reader);
		material.normalTexture    = readString(reader);
		material.ormTexture       = readString(reader);
		return material;
	}

	void
	saveMaterial(const BMaterial& material, const std::filesystem::path& path)
	{
		const auto    bytes = serializeMaterial(material);
		std::ofstream out(path, std::ios::binary);
		if (!out)
			throw std::runtime_error("bmaterial: cannot open file for writing: " + path.string());
		out.write(
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
		if (!out)
			throw std::runtime_error("bmaterial: failed to write file: " + path.string());
	}

	BMaterial
	loadMaterial(const std::filesystem::path& path)
	{
		const auto bytes = core::file::readFileBytes(path.string());
		return deserializeMaterial(bytes);
	}
}
