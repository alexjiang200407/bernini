#pragma once
#include <assetlib_structs/BMaterial.h>
#include <cstddef>

namespace assetlib
{
	/**
	 * The index of `channel` in `PbrParams::routes`.
	 *
	 * Here and not beside `PbrChannel`: `assetlib_structs` is data, so a question about a container
	 * is answered by the library that holds the answers. A consumer that cannot link this -- the
	 * renderer -- spells the cast itself, which is all this is.
	 */
	[[nodiscard]] inline constexpr size_t
	channelIndex(PbrChannel channel) noexcept
	{
		return static_cast<size_t>(channel);
	}

	/** The index of the `component`-th channel of `group` in `PbrParams::routes`. */
	[[nodiscard]] inline constexpr size_t
	channelIndex(const ChannelGroup& group, size_t component) noexcept
	{
		return channelIndex(group.first) + component;
	}

	/**
	 * Whether anything routes into `group`.
	 *
	 * A group with nothing routed bakes to no map at all, which is a complete bake rather than a
	 * missing one: the runtime substitutes white, flat normal or the factors alone.
	 */
	[[nodiscard]] inline bool
	groupIsRouted(const PbrParams& pbr, const ChannelGroup& group) noexcept
	{
		for (size_t i = 0; i < group.count; ++i)
			if (!pbr.routes[channelIndex(group, i)].texture.empty())
				return true;
		return false;
	}

	/**
	 * Refuses a material that carries no node graph: every `.bmaterial` the editor or assetlib
	 * writes has one, so a write without it is a mistake to name rather than a document to store.
	 * Only presence is checked -- the graph is the editor's, and nothing here reads it.
	 *
	 * @throws std::runtime_error naming `material` when its `editorGraph` is empty.
	 */
	void
	requireNodeGraph(const BMaterial& material);
}
