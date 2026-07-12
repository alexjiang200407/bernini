#include <assetlib/bmaterial_io.h>

#include "ByteReader.h"
#include "ByteWriter.h"

#include <core/file/file.h>

namespace assetlib
{
	namespace
	{
		constexpr uint32_t c_Magic = 0x54414D42u;  // 'B','M','A','T' little-endian

		constexpr uint16_t c_VersionMajor = 6;
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

		void
		writePbr(ByteWriter& writer, const PbrParams& pbr)
		{
			writer.writePod(pbr.baseColorFactor);
			writer.writePod(pbr.metallicFactor);
			writer.writePod(pbr.roughnessFactor);
			writeString(writer, pbr.baseColorTexture);
			writeString(writer, pbr.normalTexture);
			writeString(writer, pbr.ormTexture);
			for (const ChannelRoute& route : pbr.routes)
			{
				writeString(writer, route.texture);
				writer.writePod(route.channel);
			}
			for (const SourceStamp& stamp : pbr.routeStamps)
			{
				writer.writePod(stamp.size);
				writer.writePod(stamp.mtime);
			}
			writer.writePod(static_cast<uint32_t>(pbr.alphaMode));
			writer.writePod(pbr.alphaCutoff);
		}

		PbrParams
		readPbr(ByteReader& reader)
		{
			PbrParams pbr;
			pbr.baseColorFactor  = reader.readPod<glm::vec4>();
			pbr.metallicFactor   = reader.readPod<float>();
			pbr.roughnessFactor  = reader.readPod<float>();
			pbr.baseColorTexture = readString(reader);
			pbr.normalTexture    = readString(reader);
			pbr.ormTexture       = readString(reader);
			for (ChannelRoute& route : pbr.routes)
			{
				route.texture = readString(reader);
				route.channel = reader.readPod<uint16_t>();
			}
			for (SourceStamp& stamp : pbr.routeStamps)
			{
				stamp.size  = reader.readPod<uint64_t>();
				stamp.mtime = reader.readPod<int64_t>();
			}
			pbr.alphaMode   = static_cast<AlphaMode>(reader.readPod<uint32_t>());
			pbr.alphaCutoff = reader.readPod<float>();
			return pbr;
		}

	}

	std::vector<std::byte>
	serializeMaterial(const BMaterial& material)
	{
		ByteWriter writer;
		writer.writePod(c_Magic);
		writer.writePod(c_VersionMajor);
		writer.writePod(c_VersionMinor);

		writer.writePod(static_cast<uint32_t>(material.shadingModel));
		writer.writePod(static_cast<uint32_t>(material.mode));
		writeString(writer, material.name);
		writeString(writer, material.editorGraph);

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			writePbr(writer, material.pbr);
			break;

		case ShadingModel::kCount:
			throw std::runtime_error(
				"bmaterial: cannot serialize shading model " +
				std::to_string(static_cast<uint32_t>(material.shadingModel)));
		}

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

		BMaterial  material;
		const auto shadingModel = reader.readPod<uint32_t>();

		if (shadingModel >= static_cast<uint32_t>(ShadingModel::kCount))
			throw std::runtime_error(
				"bmaterial: unknown shading model " + std::to_string(shadingModel));

		material.shadingModel = static_cast<ShadingModel>(shadingModel);
		material.mode         = static_cast<MaterialMode>(reader.readPod<uint32_t>());
		material.name         = readString(reader);
		material.editorGraph  = readString(reader);

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			material.pbr = readPbr(reader);
			break;

		// Already excluded by the range check above; the case exists so a new model cannot be added
		// without the compiler pointing at this switch.
		case ShadingModel::kCount:
			throw std::runtime_error("bmaterial: unreadable shading model");
		}

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
		if (material.shadingModel != ShadingModel::kPbr)
			return false;

		const PbrParams& pbr = material.pbr;

		bool hasRoutes = false;

		for (size_t i = 0; i < c_LooseChannelCount; ++i)
		{
			const ChannelRoute& route = pbr.routes[i];
			if (route.texture.empty())
				continue;

			hasRoutes = true;

			// A zeroed stamp means this route was never baked; stampOf zeroes a missing file. Neither
			// can equal a live source's stamp, so both fall out of this comparison as stale.
			if (stampOf(dataRoot / route.texture) != pbr.routeStamps[i])
				return true;
		}

		// No routes: an imported, triplet-only material. It has no sources to have drifted from.
		if (!hasRoutes)
			return false;

		// Routed and every source matches -- but a bake that produced no base colour never ran.
		return pbr.baseColorTexture.empty();
	}
}
