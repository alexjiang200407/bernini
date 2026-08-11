#pragma once

namespace assetlib
{
	struct AnimationSet;
	struct BEnv;
	struct BEnvLighting;
	struct BMaterial;
	struct BMesh;
	struct BSky;
	struct Skeleton;

	/**
	 * Renders the contents of an asset as human-readable text -- the counterpart of writeObj for the
	 * non-geometric properties. Both formats are opaque binary containers, so without this the only
	 * way to answer "what is actually in this file" is to hand-decode it against the serializer.
	 *
	 * The text is for a person, not a parser: it is not stable across versions, and nothing reads it
	 * back. `assetlib_cli describe` prints it; the editor can surface it in an asset inspector.
	 */

	/**
	 * Describes a mesh: its hierarchy and pool sizes, the material file it points each submesh at, and
	 * per-submesh geometry counts, vertex layout and bounds.
	 *
	 * @param verbose When true, every submesh is listed individually. When false only a summary and
	 *        the material table are emitted, which is what you want for a mesh with hundreds of them.
	 */
	[[nodiscard]] std::string
	describe(const BMesh& mesh, bool verbose = true);

	/**
	 * Describes a material: its mode, factors, the baked texture triplet, and the per-channel routing
	 * table with each route's bake provenance.
	 *
	 * Routes are reported against `dataRoot` when one is given: each routed source is stat'd and its
	 * live stamp compared with the one recorded at bake time, so a stale bake is visible here. Passing
	 * an empty path skips that and reports the recorded stamps alone.
	 */
	[[nodiscard]] std::string
	describe(const BMaterial& material, const std::filesystem::path& dataRoot = {});

	/**
	 * Describes a sky: how the backdrop presents it, and its one radiance route with that route's bake
	 * provenance.
	 *
	 * `dataRoot` is used exactly as it is for a material -- given one, each routed source is stat'd and
	 * compared against the stamp the bake recorded, so a stale bake is visible here. Passing an empty
	 * path reports the recorded stamps alone.
	 */
	[[nodiscard]] std::string
	describe(const BSky& sky, const std::filesystem::path& dataRoot = {});

	/**
	 * Describes the lighting derived from a sky: the exposure it was measured at, and the prefilter and
	 * irradiance routes with their bake provenance.
	 *
	 * The pair is stale as a unit -- they are convolutions of the same radiance, so one having drifted
	 * makes both untrustworthy.
	 */
	[[nodiscard]] std::string
	describe(const BEnvLighting& lighting, const std::filesystem::path& dataRoot = {});

	/**
	 * Describes an environment: the `.bsky` and `.benvl` it composes.
	 *
	 * A `.benv` holds no pixels, so with a `dataRoot` the useful question is whether what it names is
	 * actually there -- each reference is resolved against the root and reported missing if it is not.
	 */
	[[nodiscard]] std::string
	describe(const BEnv& env, const std::filesystem::path& dataRoot = {});

	/** Describes a skeleton: its signature, and each bone's parent and bind pose. */
	[[nodiscard]] std::string
	describe(const Skeleton& skeleton);

	/**
	 * Describes a clip set: the skeleton it addresses, and per clip its length, rate, root motion and
	 * loop flag.
	 *
	 * Pass the skeleton the set names to have its bone names printed, and its signature checked -- a
	 * mismatch is the failure this format is hardest to see by eye.
	 */
	[[nodiscard]] std::string
	describe(const AnimationSet& animations, const Skeleton* skeleton = nullptr);

	struct BVat;

	/**
	 * Describes a VAT bake: its texture pair's dimensions and bounds, each clip's rows, each
	 * submesh's columns, and the three inputs it was baked from with their stamps.
	 *
	 * Given a `dataRoot`, each input is stat'd and compared against the stamp the bake recorded, so
	 * a stale bake is reported. Pass the tables-only form (loadVatTables) -- nothing here reads the
	 * pixels, and a whole-project survey must not pay for them.
	 */
	[[nodiscard]] std::string
	describe(const BVat& vat, const std::filesystem::path& dataRoot = {});
}
