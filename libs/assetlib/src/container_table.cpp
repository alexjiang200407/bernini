#include <algorithm>
#include <array>
#include <assetlib/AssetCodec.h>
#include <assetlib/asset_refs.h>
#include <assetlib/codecs.h>

#include <assetlib/avatar.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Skeleton.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>

namespace assetlib
{
	namespace
	{
		/**
		 * Every container type, once. The only list; `containerKinds` is derived from it and the
		 * assertions below hold it to `AssetType`, so a container added here and forgotten anywhere
		 * else does not compile.
		 *
		 * The codec-less kinds are deliberately absent: they are listed in `c_Foreign` below, and
		 * the assertion counts both.
		 */
		using Containers = std::tuple<
			BMesh,
			BMaterial,
			BEnv,
			BSky,
			BEnvLighting,
			Skeleton,
			AnimationSet,
			ImportDocument,
			Avatar>;

		template <AssetCodecFor T>
		[[nodiscard]] constexpr ContainerKind
		kindOf() noexcept
		{
			ContainerKind kind{ .type      = AssetCodec<T>::c_Type,
				                .extension = AssetCodec<T>::c_Extension };
			if constexpr (CacheEntryCodecFor<T>)
			{
				kind.bakeToken = AssetCodec<T>::c_BakeToken;
				kind.magic     = AssetCodec<T>::c_Magic;
			}
			return kind;
		}

		template <AssetCodecFor... Ts>
		[[nodiscard]] constexpr std::array<ContainerKind, sizeof...(Ts)>
		buildTable(std::tuple<Ts...>*) noexcept
		{
			return { { kindOf<Ts>()... } };
		}

		// constexpr, so it is built at compile time from the codecs and there is no initialization
		// order to get wrong -- every token is a constant expression in its own header.
		constexpr auto c_Table = buildTable(static_cast<Containers*>(nullptr));

		/**
		 * Every asset kind this library stores without encoding: an image, and the UI runtime's own
		 * text and font. Each is packed, deleted and renamed like a container and has no struct to
		 * load, so it carries an extension and nothing else.
		 */
		constexpr std::array<ForeignKind, 4> c_Foreign = { {
			{ AssetType::kTexture, c_TextureExtension },
			{ AssetType::kUiDocument, c_UiDocumentExtension },
			{ AssetType::kUiStyle, c_UiStyleExtension },
			{ AssetType::kFont, c_FontExtension },
		} };

		constexpr size_t c_AssetTypeCount = static_cast<size_t>(AssetType::kCount);

		[[nodiscard]] constexpr bool
		everyTypeListedOnce() noexcept
		{
			for (size_t i = 0; i < c_AssetTypeCount; ++i)
			{
				const auto type = static_cast<AssetType>(i);

				const size_t listed =
					static_cast<size_t>(std::ranges::count(c_Table, type, &ContainerKind::type)) +
					static_cast<size_t>(std::ranges::count(c_Foreign, type, &ForeignKind::type));

				if (listed != 1)
					return false;
			}
			return true;
		}

		// Every AssetType is either a container with a codec or a foreign kind, so a new asset type
		// has to say which it is here. Without this it would simply have no codec and drop out of
		// the migrate and the pack that read these tables -- silently, which is the failure they
		// exist to make impossible.
		//
		// Counted per type rather than in total: two rows naming one type sum to the right number
		// while leaving another type in neither table, which is what a copied row does.
		static_assert(
			everyTypeListedOnce(),
			"every AssetType must appear exactly once, as a codec in Containers or a row in "
			"c_Foreign");
	}

	std::span<const ContainerKind>
	containerKinds() noexcept
	{
		return c_Table;
	}

	std::optional<ContainerKind>
	containerKindForExtension(std::string_view extension) noexcept
	{
		const auto it = std::ranges::find(c_Table, extension, &ContainerKind::extension);
		if (it == c_Table.end())
			return std::nullopt;

		return *it;
	}

	const ContainerKind&
	containerKindFor(AssetType type) noexcept
	{
		const auto it = std::ranges::find(c_Table, type, &ContainerKind::type);
		assert(it != c_Table.end() && "the table is total over the container kinds");
		return *it;
	}

	std::span<const ForeignKind>
	foreignKinds() noexcept
	{
		return c_Foreign;
	}

	std::optional<ForeignKind>
	foreignKindForExtension(std::string_view extension) noexcept
	{
		const auto it = std::ranges::find(c_Foreign, extension, &ForeignKind::extension);
		if (it == c_Foreign.end())
			return std::nullopt;

		return *it;
	}

	std::optional<ContainerKind>
	containerKindForMagic(uint32_t magic) noexcept
	{
		if (magic == 0)
			return std::nullopt;

		const auto it = std::ranges::find(c_Table, magic, &ContainerKind::magic);
		if (it == c_Table.end())
			return std::nullopt;

		return *it;
	}
}
