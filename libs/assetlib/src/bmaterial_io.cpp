#include <algorithm>
#include <array>
#include <assetlib/bmaterial.h>
#include <assetlib/codecs.h>
#include <assetlib/container_info.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BMaterial.h>
#include <core/file/LooseFileSystem.h>

#include "json_doc.h"

#include <core/err/util.h>
#include <core/file/file.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

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

		// Every top-level key the PBR half owns, so a material of another model can be cleared of
		// them by name rather than by whatever the writer below happens to emit.
		constexpr std::array<std::string_view, 8> c_PbrKeys = { {
			"baseColorFactor",
			"metallicFactor",
			"roughnessFactor",
			"transmissionFactor",
			"specularColorFactor",
			"specularFactor",
			"baked",
			"routes",
		} };

		constexpr std::array<std::string_view, 2> c_ShadingModelNames = { {
			"pbr",
			"pbrSurface",
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
				object.erase(key);
			else
				object[key] = value;
		}

		/** Drops object members that ended empty, so a fully-taken route leaves no husk. */
		void
		eraseEmptyMembers(nlohmann::json& object)
		{
			for (auto it = object.begin(); it != object.end();)
				it = it->is_object() && it->empty() ? object.erase(it) : std::next(it);
		}

		/**
		 * Takes one route's known keys out of `route`, leaving anything preserved for the
		 * round-trip. `label` names the route in errors -- "route 'ao'", or "texture 'orm'
		 * route 'r'" -- so both halves report in their own vocabulary.
		 */
		void
		takeRoute(
			nlohmann::json&    route,
			const std::string& label,
			ChannelRoute&      out,
			SourceStamp&       stamp)
		{
			doc::Taker(route, c_What).Take("texture", out.texture);

			if (const auto channel = route.find("channel"); channel != route.end())
			{
				core::throw_runtime_error_if(
					!channel->is_number_unsigned() || channel->get<uint64_t>() > 3,
					"bmaterial: {} has an invalid channel",
					label);
				out.channel = static_cast<uint16_t>(channel->get<uint64_t>());
				route.erase(channel);
			}

			for (const auto& [stampKey, field] :
			     { std::pair<std::string_view, uint64_t*>{ "stampSize", &stamp.size },
			       { "stampHash", &stamp.hash } })
			{
				if (const auto value = route.find(stampKey); value != route.end())
				{
					core::throw_runtime_error_if(
						!value->is_number_unsigned(),
						"bmaterial: {} has an invalid {}",
						label,
						stampKey);
					*field = value->get<uint64_t>();
					route.erase(value);
				}
			}
		}

		/**
		 * Writes one route into `routes[key]`, merged over anything preserved there -- or, when
		 * the struct says nothing for it, erases its known keys and keeps the rest.
		 *
		 * A stamp can outlive its route -- a bake's provenance is not dropped with a rerouted
		 * channel -- so an entry is written whenever either half says something.
		 */
		void
		writeRoute(
			nlohmann::json&     routes,
			const std::string&  key,
			const ChannelRoute& route,
			const SourceStamp&  stamp)
		{
			if (!route.texture.empty() || stamp != SourceStamp{})
			{
				auto& entry = routes[key];
				if (!entry.is_object())
					entry = nlohmann::json::object();
				entry[c_RouteKeys[0]] = route.texture;
				entry[c_RouteKeys[1]] = route.channel;
				entry[c_RouteKeys[2]] = stamp.size;
				entry[c_RouteKeys[3]] = stamp.hash;
			}
			else if (const auto found = routes.find(key); found != routes.end())
			{
				for (const std::string_view k : c_RouteKeys) found->erase(k);
				if (found->empty())
					routes.erase(found);
			}
		}

		/**
		 * Takes `parameters`, whose members are one to four numbers each, in the document's own
		 * key order so a save writes the file back as it was read.
		 */
		void
		takeSurfaceValues(nlohmann::json& json, std::vector<SurfaceValueBinding>& out)
		{
			const auto it = json.find("parameters");
			if (it == json.end())
				return;

			core::throw_runtime_error_if(
				!it->is_object(),
				"bmaterial: 'parameters' is not an object");

			out.reserve(out.size() + it->size());
			for (const auto& [name, value] : it->items())
			{
				auto& parameter = out.emplace_back(name);

				if (value.is_number())
				{
					parameter.value.push_back(value.get<float>());
					continue;
				}

				core::throw_runtime_error_if(
					!value.is_array() || value.empty() || value.size() > 4 ||
						!std::ranges::all_of(value, [](const auto& v) { return v.is_number(); }),
					"bmaterial: parameter '{}' is not a number or an array of one to four numbers",
					name);

				for (const auto& component : value)
					parameter.value.push_back(component.get<float>());
			}

			json.erase(it);
		}

		// The slot channels a routed surface texture composites, in `routes` member order.
		constexpr std::array<std::string_view, c_SurfaceSlotChannelCount> c_SlotChannelKeys = { {
			"r",
			"g",
			"b",
			"a",
		} };

		/**
		 * Takes `textures`. A member is one mount key -- the whole binding -- or an object for a
		 * slot composited from channel routes (ADR-7 in the surface-material-panel plan):
		 * `{ "routes": { "r": { texture, channel, stampSize, stampHash }, ... },
		 *    "baked": <map>, "token": <bake token>, "texture": <whole binding, when kept> }`.
		 *
		 * Known keys come out; anything else inside a slot or a route stays and rides `extraJson`
		 * through the round-trip, exactly as the PBR `routes` do.
		 */
		void
		takeSurfaceTextures(nlohmann::json& json, std::vector<SurfaceTextureBinding>& out)
		{
			const auto it = json.find("textures");
			if (it == json.end())
				return;

			core::throw_runtime_error_if(
				!it->is_object(),
				"bmaterial: 'textures' is not an object");

			out.reserve(out.size() + it->size());
			for (auto& [name, value] : it->items())
			{
				if (value.is_string())
				{
					out.emplace_back(name, value.get<std::string>());

					// Fully taken; an empty object is what the sweep below removes.
					value = nlohmann::json::object();
					continue;
				}

				core::throw_runtime_error_if(
					!value.is_object(),
					"bmaterial: texture '{}' is not a path or a routed slot",
					name);

				auto& slot = out.emplace_back(name);

				const doc::Taker taker(value, c_What);
				taker.Take("texture", slot.texturePath);
				taker.Take("baked", slot.bakedPath);

				if (const auto token = value.find("token"); token != value.end())
				{
					core::throw_runtime_error_if(
						!token->is_number_unsigned(),
						"bmaterial: texture '{}' has an invalid bake token",
						name);
					slot.bakeToken = token->get<uint64_t>();
					value.erase(token);
				}

				const auto routes = value.find("routes");
				if (routes == value.end())
					continue;

				core::throw_runtime_error_if(
					!routes->is_object(),
					"bmaterial: texture '{}' routes are not an object",
					name);
				for (auto& [channelName, route] : routes->items())
				{
					const auto found = std::ranges::find(c_SlotChannelKeys, channelName);
					core::throw_runtime_error_if(
						found == c_SlotChannelKeys.end(),
						"bmaterial: texture '{}' routes unknown channel '{}'",
						name,
						channelName);
					core::throw_runtime_error_if(
						!route.is_object(),
						"bmaterial: texture '{}' route '{}' is not an object",
						name,
						channelName);

					const size_t index = static_cast<size_t>(found - c_SlotChannelKeys.begin());
					takeRoute(
						route,
						"texture '" + name + "' route '" + channelName + "'",
						slot.routes[index],
						slot.routeStamps[index]);
				}
				eraseEmptyMembers(*routes);
				if (routes->empty())
					value.erase(routes);
			}

			eraseEmptyMembers(*it);
			if (it->empty())
				json.erase(it);
		}

		/**
		 * Writes the PBR half, or erases every key of it when the material is not drawn by that
		 * model -- the mirror of writePbrSurface, and the same rule read from the other side.
		 *
		 * The model decides, not whether the struct happens to hold anything: PbrParams
		 * default-constructs to glTF's own defaults, so a surface material left to this writer
		 * would pick up a white base colour and a metallic of one that it never declared.
		 */
		void
		writePbr(nlohmann::json& json, const BMaterial& material)
		{
			if (material.shadingModel != ShadingModel::kPbr)
			{
				for (const std::string_view key : c_PbrKeys) json.erase(key);
				return;
			}

			const PbrParams& pbr        = material.pbr;
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
			if (pbr.bakeToken != 0)
				baked["token"] = pbr.bakeToken;
			else
				baked.erase("token");
			if (baked.empty())
				json.erase("baked");

			auto& routes = json["routes"];
			if (!routes.is_object())
				routes = nlohmann::json::object();
			for (size_t i = 0; i < c_LooseChannelCount; ++i)
				writeRoute(
					routes,
					std::string(c_ChannelNames[i]),
					pbr.routes[i],
					pbr.routeStamps[i]);
			if (routes.empty())
				json.erase("routes");
		}

		/**
		 * Writes the three keys, or erases them when the material is not drawn by a surface.
		 *
		 * `parameters` is rebuilt -- every member is a number the reader took whole -- while
		 * `textures` is merged into what `extraJson` preserved, exactly as the PBR `baked` and
		 * `routes` are: a slot's object form can nest a sibling branch's key, and it survives.
		 *
		 * The model decides, not the name: a document that stopped being a surface material still
		 * carries the keys, and writing them back would leave a material claiming a surface it is
		 * no longer drawn by.
		 */
		void
		writePbrSurface(nlohmann::json& json, const BMaterial& material)
		{
			const SurfaceParams& surface = material.surface;

			if (material.shadingModel != ShadingModel::kPbrSurface)
			{
				json.erase("surface");
				json.erase("parameters");
				json.erase("textures");
				return;
			}

			json["surface"] = surface.name;

			auto parameters = nlohmann::json::object();
			for (const SurfaceValueBinding& value : surface.values)
			{
				core::throw_runtime_error_if(
					value.value.empty() || value.value.size() > 4,
					"bmaterial: parameter '{}' holds {} numbers, and a parameter is one to four",
					value.name,
					value.value.size());

				// One number as a number: a scalar an author typed as `2.0` is written back as
				// `2.0` rather than promoted to a one-element array.
				if (value.value.size() == 1)
				{
					parameters[value.name] = doc::plainFloat(value.value.front());
					continue;
				}

				auto array = nlohmann::json::array();
				for (const float component : value.value)
					array.push_back(doc::plainFloat(component));
				parameters[value.name] = std::move(array);
			}
			if (parameters.empty())
				json.erase("parameters");
			else
				json["parameters"] = std::move(parameters);

			auto& textures = json["textures"];
			if (!textures.is_object())
				textures = nlohmann::json::object();
			for (const SurfaceTextureBinding& slot : surface.textures)
			{
				const bool routedState =
					slot.bakeToken != 0 || !slot.bakedPath.empty() || slotIsRouted(slot) ||
					std::ranges::any_of(slot.routeStamps, [](const SourceStamp& stamp) {
						return stamp != SourceStamp{};
					});

				const auto preserved = textures.find(slot.name);
				if (!routedState && (preserved == textures.end() || !preserved->is_object()))
				{
					// The whole binding, in the shorthand every pre-ADR-7 document used.
					if (!slot.texturePath.empty())
						textures[slot.name] = slot.texturePath;
					else
						textures.erase(slot.name);
					continue;
				}

				// The object form, merged: a slot keeps it once anything beyond the whole binding
				// -- route state, or a preserved key -- has to live inside it.
				auto& entry = textures[slot.name];
				if (!entry.is_object())
					entry = nlohmann::json::object();
				setOrErase(entry, "texture", slot.texturePath);
				setOrErase(entry, "baked", slot.bakedPath);
				if (slot.bakeToken != 0)
					entry["token"] = slot.bakeToken;
				else
					entry.erase("token");

				auto& routes = entry["routes"];
				if (!routes.is_object())
					routes = nlohmann::json::object();
				for (size_t i = 0; i < c_SurfaceSlotChannelCount; ++i)
					writeRoute(
						routes,
						std::string(c_SlotChannelKeys[i]),
						slot.routes[i],
						slot.routeStamps[i]);
				if (routes.empty())
					entry.erase("routes");
				if (entry.empty())
					textures.erase(slot.name);
			}
			if (textures.empty())
				json.erase("textures");
		}

		BMaterial
		materialFromDocument(std::string_view text)
		{
			auto json = doc::parseObject(text, "bmaterial: the document");

			const doc::Taker taker(json, c_What);

			BMaterial material;

			// Refused rather than defaulted when it names a model this build has not heard of: a
			// typo that silently rendered as PBR would never be found.
			std::string shadingModel(c_ShadingModelNames[0]);
			taker.Take("shadingModel", shadingModel);
			const auto model = std::ranges::find(c_ShadingModelNames, shadingModel);
			core::throw_runtime_error_if(
				model == c_ShadingModelNames.end(),
				"bmaterial: unknown shading model '{}'",
				shadingModel);
			material.shadingModel = static_cast<ShadingModel>(model - c_ShadingModelNames.begin());

			taker.Take("name", material.name);

			// A string, not an embedded object: the graph is the editor's opaque blob, promised
			// back byte-for-byte, and nothing here may assume it parses.
			taker.Take("editorGraph", material.editorGraph);

			MaterialLayer& layer = material.layer;

			std::string alphaMode(c_AlphaModeNames[0]);
			taker.Take("alphaMode", alphaMode);
			const auto mode = std::ranges::find(c_AlphaModeNames, alphaMode);
			core::throw_runtime_error_if(
				mode == c_AlphaModeNames.end(),
				"bmaterial: unknown alpha mode '{}'",
				alphaMode);
			layer.alphaMode = static_cast<AlphaMode>(mode - c_AlphaModeNames.begin());

			taker.Take("alphaCutoff", layer.alphaCutoff);
			taker.Take("doubleSided", layer.doubleSided);

			PbrParams& pbr = material.pbr;

			taker.Take("baseColorFactor", pbr.baseColorFactor);
			taker.Take("metallicFactor", pbr.metallicFactor);
			taker.Take("roughnessFactor", pbr.roughnessFactor);
			taker.Take("transmissionFactor", pbr.transmissionFactor);
			taker.Take("specularColorFactor", pbr.specularColorFactor);
			taker.Take("specularFactor", pbr.specularFactor);

			// Taken whatever the model is, so a surface's keys never ride `extraJson` back out
			// beside the ones written from the struct.
			taker.Take("surface", material.surface.name);
			takeSurfaceValues(json, material.surface.values);
			takeSurfaceTextures(json, material.surface.textures);

			// Taken, then dropped: the keys are not this material's, and a struct still holding
			// them would say it is drawn by a surface that its own model denies.
			if (material.shadingModel != ShadingModel::kPbrSurface)
				material.surface = SurfaceParams();

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
				if (const auto token = it->find("token"); token != it->end())
				{
					core::throw_runtime_error_if(
						!token->is_number_unsigned(),
						"bmaterial: 'baked.token' is not an unsigned number");
					pbr.bakeToken = token->get<uint64_t>();
					it->erase(token);
				}
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
					takeRoute(
						route,
						"route '" + channelName + "'",
						pbr.routes[index],
						pbr.routeStamps[index]);
				}
				eraseEmptyMembers(*it);
				if (it->empty())
					json.erase(it);
			}

			// The same rule from the other side: a material that is not drawn by the PBR model
			// holds none of its factors, so nothing downstream reads a white base colour off one.
			if (material.shadingModel != ShadingModel::kPbr)
				material.pbr = PbrParams();

			material.extraJson = json.dump();
			return material;
		}

	}

	std::vector<std::byte>
	AssetCodec<BMaterial>::Serialize(const BMaterial& material)
	{
		auto json = doc::parseObject(material.extraJson, "bmaterial: extraJson");

		// No default, so a new model cannot be added without the compiler pointing here.
		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
		case ShadingModel::kPbrSurface:
			json["shadingModel"] = c_ShadingModelNames[static_cast<size_t>(material.shadingModel)];
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

		const MaterialLayer& layer = material.layer;
		json["alphaMode"]          = alphaModeName(layer.alphaMode);
		json["alphaCutoff"]        = doc::plainFloat(layer.alphaCutoff);
		json["doubleSided"]        = layer.doubleSided;

		writePbr(json, material);
		writePbrSurface(json, material);

		return doc::toBytes(json);
	}

	BMaterial
	AssetCodec<BMaterial>::Deserialize(std::span<const std::byte> bytes)
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
	surfaceSlotBakeIsStale(
		const SurfaceTextureBinding&   slot,
		const core::file::IFileSystem& fileSystem)
	{
		// An unrouted slot binds whole or not at all; there is no bake to have gone stale.
		if (!slotIsRouted(slot))
			return false;

		for (size_t i = 0; i < c_SurfaceSlotChannelCount; ++i)
		{
			const ChannelRoute& route = slot.routes[i];
			if (route.texture.empty())
				continue;

			// A zeroed stamp means this route was never baked; stampOf zeroes a missing file.
			if (stampOf(fileSystem, route.texture) != slot.routeStamps[i])
				return true;
		}

		if (slot.bakeToken != c_TextureBakeToken)
			return true;

		// Routed and every source matches -- but the map has to be there to sample.
		return slot.bakedPath.empty() || stampOf(fileSystem, slot.bakedPath).size == 0;
	}

	bool
	bakeIsStale(const BMaterial& material, const core::file::IFileSystem& fileSystem)
	{
		if (material.shadingModel == ShadingModel::kPbrSurface)
			return std::ranges::any_of(
				material.surface.textures,
				[&](const SurfaceTextureBinding& slot) {
					return surfaceSlotBakeIsStale(slot, fileSystem);
				});

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

		if (pbr.bakeToken != c_TextureBakeToken)
			return true;

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
		// Loose is the renderer's per-channel PBR path; a stale *surface* slot recomposites at
		// load instead (ADR-8), so the model is checked before `pbr` means anything.
		return material.shadingModel == ShadingModel::kPbr && bakeIsStale(material, fileSystem) &&
		       routesAreOnDisk(material.pbr, fileSystem);
	}

}
