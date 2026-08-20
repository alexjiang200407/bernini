#include <assetlib/bmaterial_io.h>

#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/magic.h>
#include <core/file/LooseFileSystem.h>

#include "AssetSchemaBuilder.h"
#include "chunk_io.h"
#include "fs_util.h"

#include <core/err/util.h>
#include <core/file/file.h>
#include <core/str/string_pool.h>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr uint16_t c_VersionMajor = 11;  // 11: a chunk container with a schema
		constexpr uint16_t c_VersionMinor = 0;

		constexpr std::string_view c_What = "bmaterial";

		enum class ChunkId : uint32_t
		{
			kMaterial = 1,  // one MaterialRecord
			kStringPool,
			kPbr  // one PbrRecord, when the shading model is PBR
		};

		/** The part of a BMaterial every shading model has; its strings live in the pool. */
		struct MaterialRecord
		{
			uint32_t shadingModel;
			uint32_t nameOffset;
			uint32_t editorGraphOffset;
		};

		static_assert(sizeof(MaterialRecord) == 12);

		struct RouteRecord
		{
			uint32_t textureOffset;
			uint16_t channel;
		};

		static_assert(sizeof(RouteRecord) == 8);

		/** PbrParams as it is stored: paths as pool offsets, the routes and stamps as arrays. */
		struct PbrRecord
		{
			glm::vec4                                    baseColorFactor;
			float                                        metallicFactor;
			float                                        roughnessFactor;
			uint32_t                                     alphaMode;
			float                                        alphaCutoff;
			float                                        transmissionFactor;
			glm::vec3                                    specularColorFactor;
			float                                        specularFactor;
			uint32_t                                     baseColorTextureOffset;
			uint32_t                                     normalTextureOffset;
			uint32_t                                     ormTextureOffset;
			std::array<RouteRecord, c_LooseChannelCount> routes;
			std::array<SourceStamp, c_LooseChannelCount> routeStamps;
		};

		static_assert(sizeof(PbrRecord) == 64 + c_LooseChannelCount * (8 + 16));

		const schema::Schema&
		materialSchema()
		{
			static const schema::Schema c_Schema =
				AssetSchemaBuilder()
					.AddSourceStamp()
					.AddLayout<MaterialRecord>(
						"MaterialRecord",
						[](auto& layout) {
							layout.AddField("shadingModel", &MaterialRecord::shadingModel)
								.AddField("nameOffset", &MaterialRecord::nameOffset)
								.AddField("editorGraphOffset", &MaterialRecord::editorGraphOffset);
						})
					.AddLayout<RouteRecord>(
						"RouteRecord",
						[](auto& layout) {
							layout.AddField("textureOffset", &RouteRecord::textureOffset)
								.AddField("channel", &RouteRecord::channel);
						})
					.AddLayout<PbrRecord>(
						"PbrRecord",
						[](auto& layout) {
							layout
								.AddField(
									"baseColorFactor",
									&PbrRecord::baseColorFactor,
									glm::vec4(1.0f))
								.AddField("metallicFactor", &PbrRecord::metallicFactor, 1.0f)
								.AddField("roughnessFactor", &PbrRecord::roughnessFactor, 1.0f)
								.AddField("alphaMode", &PbrRecord::alphaMode)
								.AddField("alphaCutoff", &PbrRecord::alphaCutoff, 0.5f)
								.AddField("transmissionFactor", &PbrRecord::transmissionFactor)
								.AddField(
									"specularColorFactor",
									&PbrRecord::specularColorFactor,
									glm::vec3(1.0f))
								.AddField("specularFactor", &PbrRecord::specularFactor, 1.0f)
								.AddField(
									"baseColorTextureOffset",
									&PbrRecord::baseColorTextureOffset)
								.AddField("normalTextureOffset", &PbrRecord::normalTextureOffset)
								.AddField("ormTextureOffset", &PbrRecord::ormTextureOffset)
								.AddField("routes", &PbrRecord::routes)
								.AddField("routeStamps", &PbrRecord::routeStamps);
						})
					.Finish();
			return c_Schema;
		}

		std::vector<PbrRecord>
		packPbr(const PbrParams& pbr, core::string_pool& pool)
		{
			PbrRecord record{};
			record.baseColorFactor        = pbr.baseColorFactor;
			record.metallicFactor         = pbr.metallicFactor;
			record.roughnessFactor        = pbr.roughnessFactor;
			record.alphaMode              = static_cast<uint32_t>(pbr.alphaMode);
			record.alphaCutoff            = pbr.alphaCutoff;
			record.transmissionFactor     = pbr.transmissionFactor;
			record.specularColorFactor    = pbr.specularColorFactor;
			record.specularFactor         = pbr.specularFactor;
			record.baseColorTextureOffset = pool.add(pbr.baseColorTexture);
			record.normalTextureOffset    = pool.add(pbr.normalTexture);
			record.ormTextureOffset       = pool.add(pbr.ormTexture);
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
			{
				record.routes[i].textureOffset = pool.add(pbr.routes[i].texture);
				record.routes[i].channel       = pbr.routes[i].channel;
				record.routeStamps[i]          = pbr.routeStamps[i];
			}
			return { record };
		}

		PbrParams
		unpackPbr(const PbrRecord& record, const core::string_pool& pool)
		{
			PbrParams pbr;
			pbr.baseColorFactor     = record.baseColorFactor;
			pbr.metallicFactor      = record.metallicFactor;
			pbr.roughnessFactor     = record.roughnessFactor;
			pbr.alphaMode           = static_cast<AlphaMode>(record.alphaMode);
			pbr.alphaCutoff         = record.alphaCutoff;
			pbr.transmissionFactor  = record.transmissionFactor;
			pbr.specularColorFactor = record.specularColorFactor;
			pbr.specularFactor      = record.specularFactor;
			pbr.baseColorTexture    = pool.at(record.baseColorTextureOffset);
			pbr.normalTexture       = pool.at(record.normalTextureOffset);
			pbr.ormTexture          = pool.at(record.ormTextureOffset);
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
			{
				pbr.routes[i].texture = pool.at(record.routes[i].textureOffset);
				pbr.routes[i].channel = record.routes[i].channel;
				pbr.routeStamps[i]    = record.routeStamps[i];
			}
			return pbr;
		}
	}

	std::vector<std::byte>
	serializeMaterial(const BMaterial& material)
	{
		core::string_pool pool;
		MaterialRecord    record{};
		record.shadingModel      = static_cast<uint32_t>(material.shadingModel);
		record.nameOffset        = pool.add(material.name);
		record.editorGraphOffset = pool.add(material.editorGraph);

		chunk::Writer writer(materialSchema());
		writer.Add(ChunkId::kMaterial, std::vector<MaterialRecord>{ record });

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			writer.Add(ChunkId::kPbr, packPbr(material.pbr, pool));
			break;

		case ShadingModel::kCount:
			throw std::runtime_error(
				"bmaterial: cannot serialize shading model " +
				std::to_string(static_cast<uint32_t>(material.shadingModel)));
		}

		writer.Add(ChunkId::kStringPool, pool.bytes());
		return writer.Finish(magic::c_BMaterial, c_VersionMajor, c_VersionMinor);
	}

	BMaterial
	deserializeMaterial(std::span<const std::byte> bytes)
	{
		const chunk::Reader
			reader(bytes, magic::c_BMaterial, c_VersionMajor, c_What, materialSchema());

		const auto records = reader.Require<MaterialRecord>(ChunkId::kMaterial);
		core::throw_runtime_error_if(
			records.size() != 1,
			"bmaterial: the material chunk holds {} entries, not one",
			records.size());
		const core::string_pool pool(reader.Read<char>(ChunkId::kStringPool));

		const MaterialRecord& record = records[0];
		if (record.shadingModel >= static_cast<uint32_t>(ShadingModel::kCount))
			throw std::runtime_error(
				"bmaterial: unknown shading model " + std::to_string(record.shadingModel));

		BMaterial material;
		material.shadingModel = static_cast<ShadingModel>(record.shadingModel);
		material.name         = pool.at(record.nameOffset);
		material.editorGraph  = pool.at(record.editorGraphOffset);

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
		{
			const auto pbr = reader.Require<PbrRecord>(ChunkId::kPbr);
			core::throw_runtime_error_if(
				pbr.size() != 1,
				"bmaterial: the PBR chunk holds {} entries, not one",
				pbr.size());
			material.pbr = unpackPbr(pbr[0], pool);
			break;
		}

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

	BMaterial
	loadMaterial(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		return deserializeMaterial(fileSystem.Read(path));
	}

	namespace
	{
		// What a path hashed to, and the size and mtime it had when it did. mtime is not part of the
		// stamp any more, but it is still the cheapest evidence that a file has not been rewritten --
		// so it survives here as a cache key, where being wrong costs a re-hash rather than a wrong
		// answer. A staleness check runs per material and sources are shared between them, so
		// without this a project describe would re-read the same texture once per material.
		struct HashedFile
		{
			uint64_t                        size;
			std::filesystem::file_time_type mtime;
			uint64_t                        hash;
		};

		std::mutex                                  g_HashCacheMutex;
		std::unordered_map<std::string, HashedFile> g_HashCache;
	}

	SourceStamp
	stampOf(const std::filesystem::path& path)
	{
		std::error_code ec;

		const auto size = std::filesystem::file_size(path, ec);
		if (ec)
			return {};

		const auto mtime = std::filesystem::last_write_time(path, ec);
		if (ec)
			return {};

		const std::string key = path.string();

		{
			const std::lock_guard lock(g_HashCacheMutex);

			const auto cached = g_HashCache.find(key);
			if (cached != g_HashCache.end() && cached->second.size == size &&
			    cached->second.mtime == mtime)
				return SourceStamp{ size, cached->second.hash };
		}

		const std::optional<uint64_t> hash = core::file::hash_file(path);
		if (!hash)
			return {};

		{
			const std::lock_guard lock(g_HashCacheMutex);
			g_HashCache[key] = HashedFile{ size, mtime, *hash };
		}

		return SourceStamp{ size, *hash };
	}

	SourceStamp
	stampOf(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		// Asked first, and of the mount rather than of the host: a path that escapes the root is
		// absent here even though the OS would resolve it, and the fast path below would not know
		// that. A route pointing outside the project must read the same way against a directory as
		// against an archive, which cannot carry it at all.
		const auto stamp = fileSystem.Stat(path);
		if (!stamp.has_value())
			return {};

		// A directory resolves to a host path, which is an identity the memo above can key on. Taken
		// because it is the hot case -- a staleness sweep asks about the same texture once per
		// material, and the editor's mount is always a directory. Normalized so that two spellings
		// of one file are one cache entry.
		if (const auto* loose = dynamic_cast<const core::file::LooseFileSystem*>(&fileSystem))
			return stampOf((loose->GetRoot() / path).lexically_normal());

		// Uncached: nothing else identifies a mount well enough to key on. An archive is read once
		// at load rather than swept, so what this costs is one pass over the entry rather than a
		// re-read per material.
		const std::optional<uint64_t> hash = core::file::hash_file(fileSystem, path);
		if (!hash)
			return {};

		return SourceStamp{ stamp->size, *hash };
	}

	namespace
	{
		// Whether every map the triplet names is still on disk. An empty entry names no map: a group
		// with nothing routed is never baked, and the runtime substitutes white / flat normal for it.
		bool
		tripletIsOnDisk(const PbrParams& pbr, const core::file::IFileSystem& fileSystem)
		{
			for (const std::string* map :
			     { &pbr.baseColorTexture, &pbr.normalTexture, &pbr.ormTexture })
			{
				if (!map->empty() && stampOf(fileSystem, *map).size == 0)
					return false;
			}
			return true;
		}

		// Whether every source the routes name is still on disk, i.e. whether loose is a representation
		// this material could actually sample.
		bool
		routesAreOnDisk(const PbrParams& pbr, const core::file::IFileSystem& fileSystem)
		{
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
			{
				const std::string& texture = pbr.routes[i].texture;
				if (!texture.empty() && stampOf(fileSystem, texture).size == 0)
					return false;
			}
			return true;
		}
	}

	bool
	bakeIsStale(const BMaterial& material, const core::file::IFileSystem& fileSystem)
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
			if (stampOf(fileSystem, route.texture) != pbr.routeStamps[i])
				return true;
		}

		// No routes: an imported, triplet-only material. It has no sources to have drifted from.
		if (!hasRoutes)
			return false;

		// Routed and every source matches -- but a bake that produced no base colour never ran, and a
		// map deleted since leaves the triplet naming a file that is not there to sample.
		return pbr.baseColorTexture.empty() || !tripletIsOnDisk(pbr, fileSystem);
	}

	bool
	drawsLoose(const BMaterial& material, const core::file::IFileSystem& fileSystem)
	{
		// bakeIsStale is false for every non-PBR model, so `pbr` is only read once it means something.
		return bakeIsStale(material, fileSystem) && routesAreOnDisk(material.pbr, fileSystem);
	}
}
