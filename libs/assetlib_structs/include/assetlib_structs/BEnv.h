#pragma once
#include <assetlib_structs/ShadingModel.h>
#include <assetlib_structs/SourceStamp.h>
#include <core/glm.h>

namespace assetlib
{
	/**
	 * One authored environment map: where it is authored from, what the bake wrote, and the stamp
	 * tying the two together.
	 *
	 * The same shape as a material's channel route, and for the same reason. `source` is the artist's
	 * file under `Derived/SourceTextures/`, `baked` is the machine-ready `.ktx2` under
	 * `Derived/BakedTextures/`, and `stamp`
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
	 * The sky: one radiance cube map, purely derived from its routed source.
	 *
	 * Separate from the lighting derived out of it because the two have different lifetimes:
	 * re-authoring a sky need not trigger the minutes of convolution the lighting costs. How the
	 * backdrop *presents* it -- mip, rotation -- is authored state and lives on the `.benv`
	 * document, not here.
	 */
	struct BSky
	{
		std::string name;
		EnvMapRoute sky;
	};

	/**
	 * A rim light: the environment sampled in the direction away from the camera, weighted towards
	 * a surface's silhouette.
	 *
	 * A readability device rather than a reflectance term -- it ignores albedo, so the dark armour
	 * that needs separating from the sky most still gets it. Which surfaces catch it is the mesh's
	 * decision, not this one's.
	 */
	struct RimLight
	{
		glm::vec3 tint = glm::vec3(1.0f);

		// Zero is a rim light that is off, which is what an environment with no rim block resolves to.
		float intensity = 0.0f;

		// Falloff towards the silhouette; higher is a narrower band.
		float power = 4.0f;

		friend bool
		operator==(const RimLight&, const RimLight&) = default;
	};

	/**
	 * The half of an environment that only a PBR surface reads.
	 *
	 * The prefilter/irradiance pair is the one thing a `.benv` names that another shading model
	 * would have no use for -- a toon environment has a sky and a rim and no split-sum anything --
	 * so it sits behind the document's `shadingModel` rather than beside the sky.
	 */
	struct PbrEnvParams
	{
		std::string lighting;  // path to a `.benvl`, relative to the data root; empty when unset

		/**
		 * What a person decided this environment renders at, overruling the exposure the lighting
		 * bake derived. Unset until somebody authors it, and untouched by every re-bake.
		 */
		std::optional<float> exposureOverride;

		friend bool
		operator==(const PbrEnvParams&, const PbrEnvParams&) = default;
	};

	/**
	 * One environment, authored: the sky it draws, the rim it casts, and the lighting its shading
	 * model derives from that sky.
	 *
	 * A `.benv` holds no pixels. Composing by path is what lets a sky be re-authored without
	 * touching the lighting minutes of convolution produced, and what lets two environments share
	 * one sky. The presentation knobs live here rather than on the derived containers because
	 * they are decisions, not derivations -- a re-bake must never touch them.
	 */
	struct BEnv
	{
		std::string name;

		// Which shading model the `pbr` block below belongs to. Refused rather than defaulted when
		// the document names one this build does not have.
		ShadingModel shadingModel = ShadingModel::kPbr;

		std::string sky;  // path to a `.bsky`, relative to the data root; empty when unset

		/**
		 * Which mip of the sky the backdrop samples, as requested -- resolution clamps it to the
		 * mips the baked map actually has. Above 0 defocuses it, reading as depth of field.
		 */
		uint32_t skyMipLevel = 0;

		float skyRotationY = 0.0f;  // radians, about the up axis

		RimLight rim;

		PbrEnvParams pbr;

		// Document keys this build does not know, written back on save -- a sibling branch's new
		// field survives a round-trip through a reader that has never heard of it.
		std::string extraJson = "{}";
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
		 * The exposure `exposureFor` derived from these maps, rewritten by every bake. An HDR
		 * environment's absolute scale is arbitrary, so this has to move whenever the maps do --
		 * which is why it lives in the file and not in config.
		 *
		 * A proposal, not the answer: it normalizes every environment to middle grey, so on its
		 * own no environment can be dimmer or brighter than another. The `.benv` document's
		 * `exposureOverride` is what says so, kept there because it is a decision -- a re-bake
		 * refreshes this proposal without ever touching it.
		 */
		float exposure = 1.0f;
	};

	/** What was authored on the environment, or the bake's derivation until something is. */
	[[nodiscard]] inline float
	effectiveExposure(const BEnv& env, const BEnvLighting& lighting) noexcept
	{
		return env.pbr.exposureOverride.value_or(lighting.exposure);
	}
}
