#pragma once
#include <bgl/TextureAssetHandle.h>

namespace bgl
{
	/**
	 * The decoded IBL cube maps: the diffuse and specular convolutions of one environment.
	 *
	 * The split-sum BRDF table that completes the specular term is not here. It integrates a white
	 * environment, so it belongs to the shading model rather than to any environment, and bgl
	 * generates its own at device init -- there is nothing for a caller to supply or to mismatch.
	 */
	struct EnvironmentMapDesc
	{
		EnvironmentMapDesc() = default;

		EnvironmentMapDesc(TextureAssetHandle irr, TextureAssetHandle pre) :
			irradiance(irr), prefilter(pre)
		{}

		EnvironmentMapDesc(EnvironmentMapDesc&&) noexcept = default;
		EnvironmentMapDesc(const EnvironmentMapDesc&)     = delete;

		EnvironmentMapDesc&
		operator=(EnvironmentMapDesc&&) noexcept = default;

		EnvironmentMapDesc&
		operator=(const EnvironmentMapDesc&) = delete;

		TextureAssetHandle irradiance;
		TextureAssetHandle prefilter;
	};
}
