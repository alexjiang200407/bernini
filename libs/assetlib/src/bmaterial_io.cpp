#include <assetlib/bmaterial_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/magic.h>

#include "fs_util.h"
#include "string_io.h"

#include <core/file/file.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib
{
	using core::io::ByteReader;
	using core::io::ByteWriter;

	namespace
	{
		constexpr uint32_t c_Magic = magic::c_BMaterial;

		constexpr uint16_t c_VersionMajor = 9;
		constexpr uint16_t c_VersionMinor = 0;

		void
		writePbr(ByteWriter& writer, const PbrParams& pbr)
		{
			writer.WritePod(pbr.baseColorFactor);
			writer.WritePod(pbr.metallicFactor);
			writer.WritePod(pbr.roughnessFactor);
			writeString(writer, pbr.baseColorTexture);
			writeString(writer, pbr.normalTexture);
			writeString(writer, pbr.ormTexture);
			for (const ChannelRoute& route : pbr.routes)
			{
				writeString(writer, route.texture);
				writer.WritePod(route.channel);
			}
			for (const SourceStamp& stamp : pbr.routeStamps)
			{
				writer.WritePod(stamp.size);
				writer.WritePod(stamp.mtime);
			}
			writer.WritePod(static_cast<uint32_t>(pbr.alphaMode));
			writer.WritePod(pbr.alphaCutoff);
			writer.WritePod(pbr.transmissionFactor);
		}

		PbrParams
		readPbr(ByteReader& reader)
		{
			PbrParams pbr;
			pbr.baseColorFactor  = reader.ReadPod<glm::vec4>();
			pbr.metallicFactor   = reader.ReadPod<float>();
			pbr.roughnessFactor  = reader.ReadPod<float>();
			pbr.baseColorTexture = readString(reader);
			pbr.normalTexture    = readString(reader);
			pbr.ormTexture       = readString(reader);
			for (ChannelRoute& route : pbr.routes)
			{
				route.texture = readString(reader);
				route.channel = reader.ReadPod<uint16_t>();
			}
			for (SourceStamp& stamp : pbr.routeStamps)
			{
				stamp.size  = reader.ReadPod<uint64_t>();
				stamp.mtime = reader.ReadPod<int64_t>();
			}
			pbr.alphaMode          = static_cast<AlphaMode>(reader.ReadPod<uint32_t>());
			pbr.alphaCutoff        = reader.ReadPod<float>();
			pbr.transmissionFactor = reader.ReadPod<float>();
			return pbr;
		}

	}

	std::vector<std::byte>
	serializeMaterial(const BMaterial& material)
	{
		ByteWriter writer;
		writer.WritePod(c_Magic);
		writer.WritePod(c_VersionMajor);
		writer.WritePod(c_VersionMinor);

		writer.WritePod(static_cast<uint32_t>(material.shadingModel));
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

		return writer.Take();
	}

	BMaterial
	deserializeMaterial(std::span<const std::byte> bytes)
	{
		ByteReader reader(bytes);

		if (reader.ReadPod<uint32_t>() != c_Magic)
			throw std::runtime_error("bmaterial: bad magic");

		const auto versionMajor = reader.ReadPod<uint16_t>();
		static_cast<void>(reader.ReadPod<uint16_t>());  // minor; additive within a major

		if (versionMajor != c_VersionMajor)
			throw std::runtime_error(
				"bmaterial: unsupported version " + std::to_string(versionMajor) + " (expected " +
				std::to_string(c_VersionMajor) + "); re-bake the material");

		BMaterial  material;
		const auto shadingModel = reader.ReadPod<uint32_t>();

		if (shadingModel >= static_cast<uint32_t>(ShadingModel::kCount))
			throw std::runtime_error(
				"bmaterial: unknown shading model " + std::to_string(shadingModel));

		material.shadingModel = static_cast<ShadingModel>(shadingModel);
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
		writeFileBytes(path, serializeMaterial(material), "bmaterial");
	}

	BMaterial
	loadMaterial(const std::filesystem::path& path)
	{
		const auto bytes = core::file::read_file_bytes(path.string());
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

	namespace
	{
		// Whether every map the triplet names is still on disk. An empty entry names no map: a group
		// with nothing routed is never baked, and the runtime substitutes white / flat normal for it.
		bool
		tripletIsOnDisk(const PbrParams& pbr, const std::filesystem::path& dataRoot)
		{
			for (const std::string* map :
			     { &pbr.baseColorTexture, &pbr.normalTexture, &pbr.ormTexture })
			{
				if (!map->empty() && stampOf(dataRoot / *map).size == 0)
					return false;
			}
			return true;
		}

		// Whether every source the routes name is still on disk, i.e. whether loose is a representation
		// this material could actually sample.
		bool
		routesAreOnDisk(const PbrParams& pbr, const std::filesystem::path& dataRoot)
		{
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
			{
				const std::string& texture = pbr.routes[i].texture;
				if (!texture.empty() && stampOf(dataRoot / texture).size == 0)
					return false;
			}
			return true;
		}
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

		// Routed and every source matches -- but a bake that produced no base colour never ran, and a
		// map deleted since leaves the triplet naming a file that is not there to sample.
		return pbr.baseColorTexture.empty() || !tripletIsOnDisk(pbr, dataRoot);
	}

	bool
	drawsLoose(const BMaterial& material, const std::filesystem::path& dataRoot)
	{
		// bakeIsStale is false for every non-PBR model, so `pbr` is only read once it means something.
		return bakeIsStale(material, dataRoot) && routesAreOnDisk(material.pbr, dataRoot);
	}
}
