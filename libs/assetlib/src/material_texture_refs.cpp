#include "material_texture_refs.h"

#include <assetlib/codecs.h>
#include <assetlib_structs/BMaterial.h>

#include "json_doc.h"

namespace assetlib
{
	namespace
	{
		void
		mapOne(
			std::string&                                          key,
			const RefKind                                         kind,
			const std::function<std::string(const std::string&)>& map,
			std::vector<std::pair<std::string, RefKind>>&         seen)
		{
			if (key.empty())
				return;

			seen.emplace_back(key, kind);
			key = map(key);
		}
	}

	std::vector<std::pair<std::string, RefKind>>
	mapMaterialTextures(
		BMaterial&                                            material,
		const std::function<std::string(const std::string&)>& map)
	{
		auto seen = std::vector<std::pair<std::string, RefKind>>();

		switch (material.shadingModel)
		{
		case ShadingModel::kPbr:
			mapOne(material.pbr.baseColorTexture, RefKind::kBakedMap, map, seen);
			mapOne(material.pbr.normalTexture, RefKind::kBakedMap, map, seen);
			mapOne(material.pbr.ormTexture, RefKind::kBakedMap, map, seen);
			for (ChannelRoute& route : material.pbr.routes)
				mapOne(route.texture, RefKind::kChannelRoute, map, seen);
			break;

		case ShadingModel::kCount:
			throw std::runtime_error(
				"assetlib::mapMaterialTextures: the material names an unknown shading model, so "
				"the textures it references cannot be known");
		}

		if (material.editorGraph.empty())
			return seen;

		// Read-only: the keys are found by parsing, but the rewrite is done on the original text so
		// every other byte survives. Re-emitting the board would reformat it under a different JSON
		// writer than the editor's, and the two spellings would then take turns rewriting the file.
		const nlohmann::json parsed = nlohmann::json::parse(material.editorGraph, nullptr, false);
		if (parsed.is_discarded())
			return seen;

		auto       moves = std::vector<std::pair<std::string, std::string>>();
		const auto walk  = [&](auto&& self, const nlohmann::json& node) -> void {
			if (node.is_string())
			{
				// Every string in the board, filtered to the ones that name a texture. The rest are
				// node kinds, port names and the editor's own vocabulary, and an edge to one of
				// those would be a reference to nothing.
				const std::string& key = node.get_ref<const std::string&>();
				if (!key.ends_with(c_TextureExtension))
					return;

				seen.emplace_back(key, RefKind::kChannelRoute);

				std::string mapped = map(key);
				if (mapped != key)
					moves.emplace_back(key, std::move(mapped));
				return;
			}

			// Objects and arrays only. Iterating a primitive in nlohmann yields the value itself,
			// so recursing on one is unbounded -- and a board is mostly numbers.
			if (!node.is_structured())
				return;

			for (const auto& child : node) self(self, child);
		};
		walk(walk, parsed);

		// A mount key needs no JSON escaping, so its quoted form is in the text verbatim, and the
		// closing quote is what stops a key matching inside a longer one.
		for (const auto& [from, to] : moves)
		{
			const std::string quoted = '"' + from + '"';
			const std::string with   = '"' + to + '"';
			for (size_t at = material.editorGraph.find(quoted); at != std::string::npos;
			     at        = material.editorGraph.find(quoted, at + with.size()))
				material.editorGraph.replace(at, quoted.size(), with);
		}

		return seen;
	}
}
