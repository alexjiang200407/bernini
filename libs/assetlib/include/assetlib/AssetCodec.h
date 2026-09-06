#pragma once
#include <assetlib/asset_refs.h>
#include <assetlib/project_layout.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace assetlib
{
	/**
	 * What a codec can be written for: a container struct this library serializes.
	 *
	 * Deliberately weaker than AssetCodecFor below, and it has to be -- that one asks whether
	 * `AssetCodec<T>` is complete, so constraining the primary template with it would be circular.
	 * This says only that the parameter is the kind of thing a container is, which is enough to
	 * turn `AssetCodec<int>` into an error at the declaration rather than a puzzle at the use.
	 */
	template <typename T>
	concept AssetContainer = std::is_class_v<T>;

	/**
	 * How one container type becomes bytes and comes back, and what identifies it on disk.
	 *
	 * One specialization per container, all of them in codecs.h so the extensions, magics and bake
	 * tokens read as one table; each `Serialize` is defined in that container's own `.cpp`. The
	 * primary template is deliberately left undefined, so a type with no codec is a compile error
	 * naming the type rather than a link error naming a mangled symbol.
	 *
	 * This is the compile-time counterpart of the runtime registry a shipping engine uses -- Godot's
	 * `ResourceFormatLoader`, Unity's `ScriptedImporter`. Those can dispatch virtually because
	 * everything they load shares a base; our containers are unrelated PODs, so a virtual codec
	 * would have to return `std::any` and every caller would cast back. A deliberate deviation --
	 * see docs/assetlib_api.md.
	 */
	template <AssetContainer T>
	struct AssetCodec;

	/**
	 * A type `AssetStore::Load` and `Save` can address: one with a complete AssetCodec.
	 *
	 * `Serialize` yields bytes for every container, the text ones included -- a canonical-JSON
	 * document's bytes *are* its text, and one seam beats two that can disagree about which a
	 * container is.
	 */
	template <typename T>
	concept AssetCodecFor =
		AssetContainer<T> && requires(const T& value, std::span<const std::byte> bytes) {
			{ AssetCodec<T>::c_Extension } -> std::convertible_to<std::string_view>;
			{ AssetCodec<T>::c_Type } -> std::convertible_to<AssetType>;
			{ AssetCodec<T>::Serialize(value) } -> std::same_as<std::vector<std::byte>>;
			{ AssetCodec<T>::Deserialize(bytes) } -> std::same_as<T>;
		};

	/**
	 * A cache entry additionally carries the bake revision it was written at. A key mismatch is a
	 * cache miss the regeneration seam re-cooks; an authored document has no such thing and models
	 * none, which is what tells the two families apart without a flag saying so.
	 */
	template <typename T>
	concept CacheEntryCodecFor = AssetCodecFor<T> && requires {
		{ AssetCodec<T>::c_BakeToken } -> std::convertible_to<uint64_t>;
		{ AssetCodec<T>::c_Magic } -> std::convertible_to<uint32_t>;
	};

	/**
	 * `T`'s origin, and so which half of the data root it is written into -- the counterpart of
	 * originOf, which reads the same answer off a key.
	 *
	 * Read off the codec rather than restated as a directory list that could disagree with it, and
	 * derived here rather than declared per specialization: a cache entry is exactly a codec
	 * carrying a bake revision, so a codec cannot state an origin contradicting its own shape.
	 */
	template <AssetCodecFor T>
	[[nodiscard]] consteval AssetOrigin
	originFor() noexcept
	{
		return CacheEntryCodecFor<T> ? AssetOrigin::kDerived : AssetOrigin::kAuthored;
	}

	/** What the registry knows about one container, without naming its C++ type. */
	struct ContainerKind
	{
		AssetType        type;
		std::string_view extension;  // with the leading dot, as codecs.h spells it

		// Both 0 for an authored document: it carries no bake revision, and opens with its
		// text rather than a magic. A cache entry has both or neither -- see CacheEntryCodecFor.
		uint64_t bakeToken = 0;
		uint32_t magic     = 0;

		[[nodiscard]] bool
		IsCacheEntry() const noexcept
		{
			return bakeToken != 0;
		}
	};

	/**
	 * Every container this build can read or write, one entry each, in `AssetType` order.
	 *
	 * The single place the container list exists. It is generated from the codec specializations
	 * rather than written out, so a container cannot be registered here and forgotten in its codec
	 * or the other way round -- which is the failure this replaces: the list stood in seven places,
	 * and the seventh was a bake token whose omission makes stale files read as current.
	 */
	[[nodiscard]] std::span<const ContainerKind>
	containerKinds() noexcept;

	/** The container `extension` names, or nullopt for anything this library does not store. */
	[[nodiscard]] std::optional<ContainerKind>
	containerKindForExtension(std::string_view extension) noexcept;

	/**
	 * The container `type` names.
	 *
	 * @pre `type` is a container rather than one of `foreignKinds()`, which have no entry. Total
	 *      over the container kinds, so it never fails for one of those.
	 */
	[[nodiscard]] const ContainerKind&
	containerKindFor(AssetType type) noexcept;

	/**
	 * An asset kind this library stores but does not encode: it is packed, deleted, renamed and
	 * referenced like any other, and `Load<T>` has no struct to give for it.
	 *
	 * A texture is one because it is an image, and the UI's three because they are a runtime's own
	 * text and a font binary. Listing them is what keeps `AssetType` total: the assertion in
	 * `src/container_table.cpp` counts both tables, so a new kind is a compile error until it says
	 * which side it is on.
	 */
	struct ForeignKind
	{
		AssetType        type;
		std::string_view extension;  // with the leading dot, as codecs.h spells it
	};

	/** Every asset kind with no codec, one entry each. */
	[[nodiscard]] std::span<const ForeignKind>
	foreignKinds() noexcept;

	/** The codec-less kind `extension` names, or nullopt when a codec claims it or nothing does. */
	[[nodiscard]] std::optional<ForeignKind>
	foreignKindForExtension(std::string_view extension) noexcept;

	/**
	 * The cache entry whose files open with `magic`, or nullopt for anything else.
	 *
	 * Never answers for an authored document: those are text and open with their content, so a
	 * reader tells them apart by extension after deciding the file is text at all.
	 */
	[[nodiscard]] std::optional<ContainerKind>
	containerKindForMagic(uint32_t magic) noexcept;
}
