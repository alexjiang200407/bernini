#pragma once
#include <assetlib/asset_refs.h>

namespace assetlib
{
	struct BMaterial;

	/**
	 * Applies `map` to every texture key `material` holds, and reports what it saw.
	 *
	 * A material names its textures in three places -- the triplet its last bake wrote, the sources
	 * it routes each channel from, and the texture nodes inside `editorGraph`. The third is the one
	 * that is easy to forget and the one that wins: the editor compiles the board back into
	 * `routes`, so a graph left naming a moved file quietly undoes the move the next time anyone
	 * opens the material. Both the reference graph and the rename go through here so neither can
	 * learn about a fourth place without the other.
	 *
	 * The graph's schema is the editor's and this knows none of it: a reference is a mount key, so
	 * every JSON string in it is offered to `map` and only what `map` changes is touched. Graph text
	 * that will not parse is left exactly as it stands.
	 *
	 * @param map Returns the key to store instead, or its argument to leave it alone.
	 * @return Every key seen, with the kind holding it, in the order encountered.
	 */
	std::vector<std::pair<std::string, RefKind>>
	mapMaterialTextures(
		BMaterial&                                            material,
		const std::function<std::string(const std::string&)>& map);
}
