#pragma once
#include <assetlib/asset_refs.h>
#include <assetlib/container_format.h>

namespace assetlib
{
	/**
	 * How one container type becomes bytes and comes back, and what identifies it on disk.
	 *
	 * One specialization per container, declared beside that container's io -- `AssetCodec<BMesh>`
	 * lives in `bmesh_io.h`, next to the `serialize` it calls. The primary template is deliberately
	 * left undefined, so a type with no codec is a compile error naming the type rather than a
	 * link error naming a mangled symbol.
	 *
	 * This is the compile-time counterpart of the runtime registry a shipping engine uses -- Godot's
	 * `ResourceFormatLoader`, Unity's `ScriptedImporter`. Those can dispatch virtually because
	 * everything they load shares a base; our containers are unrelated PODs, so a virtual codec
	 * would have to return `std::any` and every caller would cast back. See
	 * `docs/specs/assetlib_store_codecs.md`.
	 */
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
	};

	/** What the registry knows about one container, without naming its C++ type. */
	struct ContainerKind
	{
		AssetType        type;
		std::string_view extension;  // with the leading dot, as container_format.h spells it

		// 0 for an authored document, which carries no bake revision.
		uint64_t bakeToken = 0;

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

	/** The container `type` names. Total over AssetType, so this never fails. */
	[[nodiscard]] const ContainerKind&
	containerKindFor(AssetType type) noexcept;
}
