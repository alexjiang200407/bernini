#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib_structs/magic.h>

namespace assetlib
{
	/**
	 * The file extension that names each container this library writes.
	 *
	 * One place, because an extension was spelled out once per consumer -- the reference scan, the
	 * texture prune, the material bake -- so a new container, or a renamed one, meant editing
	 * several files in lockstep or watching them drift.
	 *
	 * The extension is the identity a file browser sees; it never opens the file. The magic is the
	 * identity a reader trusts, and lives beside the structs it heads
	 * (`assetlib_structs/magic.h`). See `assetTypeFromExtension` for the former, the CLI's `sniff`
	 * for the latter.
	 */

	inline constexpr std::string_view c_MeshExtension        = ".bmesh";
	inline constexpr std::string_view c_MaterialExtension    = ".bmaterial";
	inline constexpr std::string_view c_TextureExtension     = ".ktx2";
	inline constexpr std::string_view c_EnvironmentExtension = ".benv";
	inline constexpr std::string_view c_SkyExtension         = ".bsky";
	inline constexpr std::string_view c_EnvLightingExtension = ".benvl";
	inline constexpr std::string_view c_SkeletonExtension    = ".bskel";
	inline constexpr std::string_view c_AnimationExtension   = ".banim";

	// Text, not a container: the authored half of one imported source, beside its `.glb`.
	inline constexpr std::string_view c_ImportDocumentExtension = ".bimport";

	// Text: the authored half of one rig, found by convention from its `.bskel`. See avatarKeyFor.
	inline constexpr std::string_view c_AvatarExtension = ".bavatar";

	// Not an asset either, and the only source kind an import takes: the file a `.bimport`
	// describes, copied into the project beside it. `assetTypeFromExtension` does not know it, so a
	// plan that has to reach one asks by this rather than by type.
	inline constexpr std::string_view c_ImportedSourceExtension = ".glb";

	// The archive the others are packed into, rather than an asset: nothing references a `.bpak`,
	// and assetTypeFromExtension does not know it.
	inline constexpr std::string_view c_PakExtension = ".bpak";

	/**
	 * The one form every reference path is keyed and stored in: lexically normal, generic
	 * separators. `Derived/BakedTextures/x.ktx2` and `./Derived/Meshes/../BakedTextures/x.ktx2` are
	 * one asset, and only this says so -- compare a requested path against a stored one through
	 * here, never raw.
	 *
	 * Beside the extensions above because both answer the same question: how a container is named
	 * and addressed, as opposed to what is inside it.
	 */
	[[nodiscard]] std::string
	normalizePath(std::string_view path);

	struct AnimationSet;
	struct Avatar;
	struct BEnv;
	struct BEnvLighting;
	struct BMaterial;
	struct BMesh;
	struct BSky;
	struct ImportDocument;
	struct Skeleton;

	/**
	 * Every container this library reads or writes, one specialization each, in `AssetType` order.
	 *
	 * The whole registration surface: a new container is a specialization here plus an entry in
	 * `Containers` (`src/container_table.cpp`), and a static assertion holds that tuple to
	 * `AssetType`. `containerKinds()` is folded out of the two, so the extension, the type, the
	 * magic and the bake token cannot drift apart -- they are only ever written once, below.
	 *
	 * Each `Serialize` / `Deserialize` is *defined* in that container's own `.cpp`, which is where
	 * the format actually lives. Declaring them here rather than in nine headers is what makes the
	 * table readable as a table: a bake token is only meaningful next to its neighbours, since what
	 * it has to be is "not what it was".
	 *
	 * A `c_BakeToken` is bumped to a fresh random value whenever its writer's layout or meaning
	 * changes; `TokenCanary_test` pins each writer's output hash beside its token and fails on a
	 * layout change that forgot to. An authored document declares neither token nor magic -- it
	 * opens with its text and has no bake revision, which is what `CacheEntryCodecFor` reads to
	 * tell the two families apart without a flag saying so.
	 */

	/** `.bmesh` -- a cache entry. Serialize refuses joints that name no skeleton; see isSkinned. */
	template <>
	struct AssetCodec<BMesh>
	{
		static constexpr std::string_view c_Extension = c_MeshExtension;
		static constexpr AssetType        c_Type      = AssetType::kMesh;
		static constexpr uint32_t         c_Magic     = magic::c_BMesh;
		static constexpr uint64_t         c_BakeToken = 0xb3407e9d1c58a2f6ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BMesh& value);

		[[nodiscard]] static BMesh
		Deserialize(std::span<const std::byte> bytes);
	};

	/** `.bmaterial` -- an authored document: canonical JSON, unknown keys preserved. */
	template <>
	struct AssetCodec<BMaterial>
	{
		static constexpr std::string_view c_Extension = c_MaterialExtension;
		static constexpr AssetType        c_Type      = AssetType::kMaterial;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BMaterial& value);

		[[nodiscard]] static BMaterial
		Deserialize(std::span<const std::byte> bytes);
	};

	/** `.benv` -- an authored document: the env family's composition and presentation knobs. */
	template <>
	struct AssetCodec<BEnv>
	{
		static constexpr std::string_view c_Extension = c_EnvironmentExtension;
		static constexpr AssetType        c_Type      = AssetType::kEnvironment;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BEnv& value);

		[[nodiscard]] static BEnv
		Deserialize(std::span<const std::byte> bytes);
	};

	/** `.bsky` -- a cache entry. */
	template <>
	struct AssetCodec<BSky>
	{
		static constexpr std::string_view c_Extension = c_SkyExtension;
		static constexpr AssetType        c_Type      = AssetType::kSky;
		static constexpr uint32_t         c_Magic     = magic::c_BSky;
		static constexpr uint64_t         c_BakeToken = 0x7c25e8b1904dfa36ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BSky& value);

		[[nodiscard]] static BSky
		Deserialize(std::span<const std::byte> bytes);
	};

	/** `.benvl` -- a cache entry. */
	template <>
	struct AssetCodec<BEnvLighting>
	{
		static constexpr std::string_view c_Extension = c_EnvLightingExtension;
		static constexpr AssetType        c_Type      = AssetType::kEnvLighting;
		static constexpr uint32_t         c_Magic     = magic::c_BEnvL;
		static constexpr uint64_t         c_BakeToken = 0xd48f19c7a35b062eull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BEnvLighting& value);

		[[nodiscard]] static BEnvLighting
		Deserialize(std::span<const std::byte> bytes);
	};

	/**
	 * `.bskel` -- a cache entry. Deserialize validates as it reads: an out-of-order or out-of-range
	 * parent is a malformed file, not something a caller has to re-check.
	 */
	template <>
	struct AssetCodec<Skeleton>
	{
		static constexpr std::string_view c_Extension = c_SkeletonExtension;
		static constexpr AssetType        c_Type      = AssetType::kSkeleton;
		static constexpr uint32_t         c_Magic     = magic::c_BSkel;
		static constexpr uint64_t         c_BakeToken = 0x9be47d02a15c68f3ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const Skeleton& value);

		[[nodiscard]] static Skeleton
		Deserialize(std::span<const std::byte> bytes);
	};

	/** `.banim` -- a cache entry. Deserialize refuses a clip whose samples fall outside the pool. */
	template <>
	struct AssetCodec<AnimationSet>
	{
		static constexpr std::string_view c_Extension = c_AnimationExtension;
		static constexpr AssetType        c_Type      = AssetType::kAnimation;
		static constexpr uint32_t         c_Magic     = magic::c_BAnim;
		static constexpr uint64_t         c_BakeToken = 0x5a1c9e37b284f0d1ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const AnimationSet& value);

		[[nodiscard]] static AnimationSet
		Deserialize(std::span<const std::byte> bytes);
	};

	/**
	 * `.bimport` -- an authored document, and the one a person edits by hand. Its canonical JSON is
	 * the bytes verbatim, so reading one back as text is a reinterpretation and never a conversion.
	 */
	template <>
	struct AssetCodec<ImportDocument>
	{
		static constexpr std::string_view c_Extension = c_ImportDocumentExtension;
		static constexpr AssetType        c_Type      = AssetType::kImportDocument;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const ImportDocument& value);

		[[nodiscard]] static ImportDocument
		Deserialize(std::span<const std::byte> bytes);
	};

	/**
	 * `.bavatar` -- an authored document: one rig's authored half, found by convention from the
	 * `.bskel` it belongs to rather than by anything naming it.
	 */
	template <>
	struct AssetCodec<Avatar>
	{
		static constexpr std::string_view c_Extension = c_AvatarExtension;
		static constexpr AssetType        c_Type      = AssetType::kAvatar;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const Avatar& value);

		[[nodiscard]] static Avatar
		Deserialize(std::span<const std::byte> bytes);
	};
}
