#include <assetlib/AssetCodec.h>

#include <assetlib/banim_io.h>
#include <assetlib/benv_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>
#include <assetlib/import_document.h>
#include <assetlib_structs/Animation.h>
#include <assetlib_structs/BEnv.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BVat.h>
#include <assetlib_structs/Skeleton.h>

namespace assetlib
{
	namespace
	{
		/**
		 * Every container type, once. The only list; `containerKinds` is derived from it and the
		 * assertions below hold it to `AssetType`, so a container added here and forgotten anywhere
		 * else does not compile.
		 *
		 * `.ktx2` is deliberately absent: a texture is an image this library encodes, not a
		 * container it serializes a struct into, so it has no codec and `AssetType::kTexture` is
		 * the one entry the table carries without one.
		 */
		using Containers = std::tuple<
			BMesh,
			BMaterial,
			BEnv,
			BSky,
			BEnvLighting,
			Skeleton,
			AnimationSet,
			BVat,
			ImportDocument>;

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

		constexpr size_t c_AssetTypeCount = static_cast<size_t>(AssetType::kCount);

		// Every AssetType is either a container with a codec or the one texture case, so a new
		// asset type has to say which it is here. Without this it would simply have no codec and
		// drop out of the migrate and the pack that read this table -- silently, which is the
		// failure the table exists to make impossible.
		static_assert(
			std::tuple_size_v<Containers> + 1 == c_AssetTypeCount,
			"every AssetType but kTexture must have a codec listed in Containers");
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
		assert(it != c_Table.end() && "the table is total over AssetType but kTexture");
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
