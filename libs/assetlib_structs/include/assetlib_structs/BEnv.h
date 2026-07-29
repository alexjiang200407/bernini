#pragma once
#include <assetlib_structs/SourceStamp.h>

namespace assetlib
{
	/**
	 * One authored environment map: where it is authored from, what the bake wrote, and the stamp
	 * tying the two together.
	 *
	 * The same shape as a material's channel route, and for the same reason. `source` is the artist's
	 * file under `textures_src/`, `baked` is the machine-ready `.ktx2` under `Textures/`, and `stamp`
	 * is `source` as it measured when `baked` was written -- so a bake that has fallen behind is
	 * detectable without decoding either image.
	 */
	struct EnvMapRoute
	{
		std::string source;  // relative to the data root; empty when unrouted
		std::string baked;   // relative to the data root; empty until a bake writes it
		SourceStamp stamp;

		friend bool
		operator==(const EnvMapRoute&, const EnvMapRoute&) = default;
	};

	/**
	 * The sky: one radiance cube map, and how the backdrop presents it.
	 *
	 * Separate from the lighting derived out of it because the two have different lifetimes. Rotating
	 * a sky or sampling a blurrier mip of it is a change a person makes and looks at immediately;
	 * re-convolving the lighting is minutes of work that the same change need not trigger.
	 */
	struct BSky
	{
		std::string name;
		EnvMapRoute sky;

		// Which mip of `sky` the backdrop samples. Above 0 defocuses it, reading as depth of field.
		uint32_t mipLevel = 0;

		float rotationY = 0.0f;  // radians, about the up axis
	};

	/**
	 * One environment, by reference: the sky it draws and the lighting derived from that sky.
	 *
	 * A `.benv` holds no pixels. Composing by path is what lets a sky be re-authored without
	 * touching the lighting minutes of convolution produced, and what lets two environments share
	 * one sky. Weather joins later through the container's minor version, which is why this exists
	 * at all rather than the editor naming the pair itself.
	 */
	struct BEnv
	{
		std::string name;
		std::string sky;       // path to a `.bsky`, relative to the data root; empty when unset
		std::string lighting;  // path to a `.benvl`, relative to the data root; empty when unset
	};

	/**
	 * The image-based lighting derived from one environment: the specular and diffuse convolutions of
	 * its radiance, plus the exposure they were measured at.
	 *
	 * The two maps are halves of one thing -- convolutions of the *same* radiance, in the same units --
	 * so they are authored, stamped and baked together. A pair drawn from different sources disagrees
	 * about how bright the world is, and does it quietly, because each looks plausible alone.
	 */
	struct BEnvLighting
	{
		std::string name;
		EnvMapRoute prefilter;   // GGX split-sum chain, one mip per roughness
		EnvMapRoute irradiance;  // clamped-cosine convolution, one mip

		/**
		 * The exposure this environment renders at. An HDR environment's absolute scale is arbitrary,
		 * so this is a property of the maps rather than of the scene, and it has to be re-derived
		 * whenever they change -- which is why it lives in the file and not in config.
		 */
		float exposure = 1.0f;
	};
}
