#pragma once
#include <core/file/IFileSystem.h>
#include <string>

namespace assetlib
{
	struct AnimationSet;
	struct BEnv;
	struct BEnvLighting;
	struct BMaterial;
	struct BMesh;
	struct BSky;
	struct Skeleton;
	struct Avatar;

	/**
	 * Renders the contents of an asset as human-readable text -- the counterpart of writeObj for the
	 * non-geometric properties. Both formats are opaque binary containers, so without this the only
	 * way to answer "what is actually in this file" is to hand-decode it against the serializer.
	 *
	 * The text is for a person, not a parser: it is not stable across versions, and nothing reads it
	 * back.
	 *
	 * **Internal.** `AssetStore::Describe` is the public door, because the useful question about a
	 * container is whether what it records is still true, and only a project can answer that. The
	 * overloads taking an `IFileSystem*` below are what it calls; a null mount reports what the
	 * container records and stops, so one body serves both rather than two that can drift.
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
	 * The recorded stamps alone; AssetStore::Describe is the form that checks them.
	 */
	[[nodiscard]] std::string
	describe(const BMaterial& material);

	/**
	 * Describes a sky: how the backdrop presents it, and its one radiance route with that route's bake
	 * provenance.
	 *
	 * The recorded stamps alone, as for a material.
	 */
	[[nodiscard]] std::string
	describe(const BSky& sky);

	/**
	 * Describes the lighting derived from a sky: the exposure it was measured at, and the prefilter and
	 * irradiance routes with their bake provenance.
	 *
	 * The pair is stale as a unit -- they are convolutions of the same radiance, so one having drifted
	 * makes both untrustworthy.
	 */
	[[nodiscard]] std::string
	describe(const BEnvLighting& lighting);

	/**
	 * Describes an environment: the `.bsky` and `.benvl` it composes.
	 *
	 * A `.benv` holds no pixels, so the useful question is whether what it names is actually there --
	 * which only AssetStore::Describe can answer.
	 */
	[[nodiscard]] std::string
	describe(const BEnv& env);

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

	/**
	 * Describes an avatar: the bone names each leg is authored with.
	 *
	 * Pass the skeleton the avatar sits by convention beside to have each name resolved -- whether
	 * the names still name bones is the whole question about an avatar, and a rig re-exported under
	 * a renamed joint is what makes the answer change.
	 */
	[[nodiscard]] std::string
	describe(const Avatar& avatar, const Skeleton* skeleton = nullptr);

	/**
	 * The mounted form of describe: each routed source stamped, so a stale bake is visible.
	 *
	 * @param fileSystem Null to report what the container records and stop, which is what the public
	 *        `describe` overloads are -- one body serves both rather than two that can drift.
	 */
	[[nodiscard]] std::string
	describe(const BMaterial& material, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BSky& sky, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BEnvLighting& lighting, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BEnv& env, const core::file::IFileSystem* fileSystem);
}
