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
	 * Whether the model's material half is a registered surface's declared parameters -- the
	 * document carries a surface name, `parameters` and `textures` rather than the PBR triplet.
	 * True for both surface contracts: which one only decides who lights the pixel, never what
	 * the document holds.
	 */
	[[nodiscard]] inline constexpr bool
	isSurfaceModel(ShadingModel model) noexcept
	{
		return model == ShadingModel::kPbrSurface || model == ShadingModel::kLitSurface;
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
}
