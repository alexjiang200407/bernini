#include <assetlib/bmaterial_io.h>

#include <assetlib/container_info.h>
#include <assetlib_structs/BMaterial.h>
#include <core/file/LooseFileSystem.h>

#include "fs_util.h"
#include "json_doc.h"

#include <core/err/util.h>
#include <core/file/file.h>
#include <nlohmann/json.hpp>

#include "mounted_io.h"

namespace assetlib
{
	namespace
	{
		constexpr std::string_view c_What = "bmaterial";

		constexpr std::array<std::string_view, c_LooseChannelCount> c_ChannelNames = { {
			"baseColorR",
			"baseColorG",
			"baseColorB",
			"baseColorA",
			"ao",
			"roughness",
			"metallic",
			"normalX",
			"normalY",
		} };

		// One list, written and erased from alike -- a key added to one half and not the other
		// would leave a stale value on a de-routed channel.
		constexpr std::array<std::string_view, 4> c_RouteKeys = { {
			"texture",
			"channel",
			"stampSize",
			"stampHash",
		} };

		constexpr std::array<std::string_view, 4> c_AlphaModeNames = { {
			"opaque",
			"mask",
			"blend",
			"hashed",
		} };

		/** No default, so a new mode cannot be added without the compiler pointing here. */
		std::string_view
		alphaModeName(AlphaMode mode)
		{
			switch (mode)
			{
			case AlphaMode::kOpaque:
				return c_AlphaModeNames[0];
			case AlphaMode::kMask:
				return c_AlphaModeNames[1];
			case AlphaMode::kBlend:
				return c_AlphaModeNames[2];
			case AlphaMode::kHashed:
				return c_AlphaModeNames[3];
			}
			throw std::runtime_error("bmaterial: unwritable alpha mode");
		}

		/** Writes `value` at `key`, or erases the key when the value is empty. */
		void
		setOrErase(nlohmann::json& object, std::string_view key, const std::string& value)
		{
			if (value.empty())
				object.erase(std::string(key));
			else
				object[std::string(key)] = value;
		}

		/** Drops object members that ended empty, so a fully-taken route leaves no husk. */
		void
		eraseEmptyMembers(nlohmann::json& object)
		{
			for (auto it = object.begin(); it != object.end();)
				it = it->is_object() && it->empty() ? object.erase(it) : std::next(it);
		}

		BMaterial
		materialFromDocument(std::string_view text)
		{
			auto json = doc::parseObject(text, "bmaterial: the document");

			const doc::Taker taker(json, c_What);

			BMaterial material;

			// The one model there is; refused rather than defaulted when it names another, since a
			// typo that silently rendered as PBR would never be found.
			std::string shadingModel = "pbr";
			taker.Take("shadingModel", shadingModel);
			core::throw_runtime_error_if(
				shadingModel != "pbr",
				"bmaterial: unknown shading model '{}'",
				shadingModel);
			material.shadingModel = ShadingModel::kPbr;

			taker.Take("name", material.name);

			// A string, not an embedded object: the graph is the editor's opaque blob, promised
			// back byte-for-byte, and nothing here may assume it parses.
			taker.Take("editorGraph", material.editorGraph);

			PbrParams& pbr = material.pbr;

			std::string alphaMode(c_AlphaModeNames[0]);
			taker.Take("alphaMode", alphaMode);
			const auto mode = std::ranges::find(c_AlphaModeNames, alphaMode);
			core::throw_runtime_error_if(
				mode == c_AlphaModeNames.end(),
				"bmaterial: unknown alpha mode '{}'",
				alphaMode);
			pbr.alphaMode = static_cast<AlphaMode>(mode - c_AlphaModeNames.begin());

			taker.Take("alphaCutoff", pbr.alphaCutoff);
			taker.Take("baseColorFactor", pbr.baseColorFactor);
			taker.Take("metallicFactor", pbr.metallicFactor);
			taker.Take("roughnessFactor", pbr.roughnessFactor);
			taker.Take("transmissionFactor", pbr.transmissionFactor);
			taker.Take("specularColorFactor", pbr.specularColorFactor);
			taker.Take("specularFactor", pbr.specularFactor);

			// Known keys come out; what remains -- a sibling branch's field at any depth -- stays
			// in the json and rides `extraJson` through the round-trip.
			if (const auto it = json.find("baked"); it != json.end())
			{
				core::throw_runtime_error_if(
					!it->is_object(),
					"bmaterial: 'baked' is not an object");
				const doc::Taker baked(*it, c_What);
				baked.Take("baseColor", pbr.baseColorTexture);
				baked.Take("normal", pbr.normalTexture);
				baked.Take("orm", pbr.ormTexture);
				if (it->empty())
					json.erase(it);
			}

			if (const auto it = json.find("routes"); it != json.end())
			{
				core::throw_runtime_error_if(
					!it->is_object(),
					"bmaterial: 'routes' is not an object");
				for (auto& [channelName, route] : it->items())
				{
					const auto found = std::ranges::find(c_ChannelNames, channelName);
					core::throw_runtime_error_if(
						found == c_ChannelNames.end(),
						"bmaterial: unknown route channel '{}'",
						channelName);
					core::throw_runtime_error_if(
						!route.is_object(),
						"bmaterial: route '{}' is not an object",
						channelName);

					const size_t index = static_cast<size_t>(found - c_ChannelNames.begin());
					doc::Taker(route, c_What).Take("texture", pbr.routes[index].texture);

					uint64_t channel = 0;
					if (const auto channelValue = route.find("channel");
					    channelValue != route.end())
					{
						core::throw_runtime_error_if(
							!channelValue->is_number_unsigned() ||
								channelValue->get<uint64_t>() > 3,
							"bmaterial: route '{}' has an invalid channel",
							channelName);
						channel = channelValue->get<uint64_t>();
						route.erase(channelValue);
					}
					pbr.routes[index].channel = static_cast<uint16_t>(channel);

					for (const auto& [stampKey, field] :
					     { std::pair<std::string_view, uint64_t*>{ "stampSize",
					                                               &pbr.routeStamps[index].size },
					       { "stampHash", &pbr.routeStamps[index].hash } })
					{
						if (const auto stampValue = route.find(stampKey); stampValue != route.end())
						{
							core::throw_runtime_error_if(
								!stampValue->is_number_unsigned(),
								"bmaterial: route '{}' has an invalid {}",
								channelName,
								stampKey);
							*field = stampValue->get<uint64_t>();
							route.erase(stampValue);
						}
					}
				}
				eraseEmptyMembers(*it);
				if (it->empty())
					json.erase(it);
			}

			material.extraJson = json.dump();
			return material;
		}

	}

	std::vector<std::byte>
	serializeMaterial(const BMaterial& material)
	{
		auto json = doc::parseObject(material.extraJson, "bmaterial: extraJson");

		// No default, so a new model cannot be added without the compiler pointing here.
		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			json["shadingModel"] = "pbr";
			break;
		case ShadingModel::kCount:
			throw std::runtime_error("bmaterial: unwritable shading model");
		}

		json["name"] = material.name;

		// A string, not an embedded object -- see materialFromDocument.
		if (!material.editorGraph.empty())
			json["editorGraph"] = material.editorGraph;
		else
			json.erase("editorGraph");

		const PbrParams& pbr        = material.pbr;
		json["alphaMode"]           = alphaModeName(pbr.alphaMode);
		json["alphaCutoff"]         = doc::plainFloat(pbr.alphaCutoff);
		json["baseColorFactor"]     = doc::vecToJson(pbr.baseColorFactor);
		json["metallicFactor"]      = doc::plainFloat(pbr.metallicFactor);
		json["roughnessFactor"]     = doc::plainFloat(pbr.roughnessFactor);
		json["transmissionFactor"]  = doc::plainFloat(pbr.transmissionFactor);
		json["specularColorFactor"] = doc::vecToJson(pbr.specularColorFactor);
		json["specularFactor"]      = doc::plainFloat(pbr.specularFactor);

		// Merged into whatever `extraJson` preserved rather than rebuilt, so a sibling branch's
		// key inside `baked` or a route survives this writer too.
		auto& baked = json["baked"];
		if (!baked.is_object())
			baked = nlohmann::json::object();
		setOrErase(baked, "baseColor", pbr.baseColorTexture);
		setOrErase(baked, "normal", pbr.normalTexture);
		setOrErase(baked, "orm", pbr.ormTexture);
		if (baked.empty())
			json.erase("baked");

		auto& routes = json["routes"];
		if (!routes.is_object())
			routes = nlohmann::json::object();
		for (size_t i = 0; i < c_LooseChannelCount; ++i)
		{
			const std::string channelName(c_ChannelNames[i]);

			// A stamp can outlive its route -- a bake's provenance is not dropped with a rerouted
			// channel -- so an entry is written whenever either half says something.
			if (!pbr.routes[i].texture.empty() || pbr.routeStamps[i] != SourceStamp{})
			{
				auto& route = routes[channelName];
				if (!route.is_object())
					route = nlohmann::json::object();
				route[std::string(c_RouteKeys[0])] = pbr.routes[i].texture;
				route[std::string(c_RouteKeys[1])] = pbr.routes[i].channel;
				route[std::string(c_RouteKeys[2])] = pbr.routeStamps[i].size;
				route[std::string(c_RouteKeys[3])] = pbr.routeStamps[i].hash;
			}
			else if (const auto found = routes.find(channelName); found != routes.end())
			{
				// The struct says nothing for this channel any more; its known keys go, anything
				// preserved stays.
				for (const std::string_view key : c_RouteKeys) found->erase(std::string(key));
				if (found->empty())
					routes.erase(found);
			}
		}
		if (routes.empty())
			json.erase("routes");

		return doc::toBytes(json);
	}

	BMaterial
	deserializeMaterial(std::span<const std::byte> bytes)
	{
		core::throw_runtime_error_if(
			!isTextAssetDocument(bytes),
			"bmaterial: not a text document; a chunk-era file is no longer convertible -- "
			"re-author the material");
		return materialFromDocument(
			std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
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

		// Routed and every source matches -- but a map deleted since leaves the triplet naming a file
		// that is not there to sample, and a base colour missing where something routes into one is a
		// bake that never ran.
		//
		// Only where something routes into one: a material tinted by its factors alone bakes no base
		// colour and is complete without one. A glass eye is the standing example -- baseColorFactor
		// carrying its alpha, a normal and an orm map, and nothing routed to base colour at all.
		return (groupIsRouted(pbr, c_BaseColorChannels) && pbr.baseColorTexture.empty()) ||
		       !tripletIsOnDisk(pbr, fileSystem);
	}

	bool
	drawsLoose(const BMaterial& material, const core::file::IFileSystem& fileSystem)
	{
		// bakeIsStale is false for every non-PBR model, so `pbr` is only read once it means something.
		return bakeIsStale(material, fileSystem) && routesAreOnDisk(material.pbr, fileSystem);
	}

	std::vector<std::byte>
	AssetCodec<BMaterial>::Serialize(const BMaterial& value)
	{
		return serializeMaterial(value);
	}

	BMaterial
	AssetCodec<BMaterial>::Deserialize(std::span<const std::byte> bytes)
	{
		return deserializeMaterial(bytes);
	}
}
