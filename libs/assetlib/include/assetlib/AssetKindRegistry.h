#pragma once

#include <assetlib/IAssetPlugin.h>
#include <core/str/str.h>
#include <span>
#include <string_view>
#include <vector>

namespace assetlib
{
	/** Owns custom authored kinds for one project lifetime. Registration is complete before stores open. */
	class AssetKindRegistry final : public IAssetKindRegistry
	{
	public:
		void
		Add(AssetKindPtr kind) override;

		/** Adds every kind after checking the whole batch; a collision leaves both unchanged. */
		void
		Merge(AssetKindRegistry&& other);

		[[nodiscard]] const IAssetKind*
		FindByExtension(std::string_view extension) const noexcept;

		[[nodiscard]] const IAssetKind*
		FindById(std::string_view id) const noexcept;

		[[nodiscard]] std::span<const AssetKindPtr>
		Kinds() const noexcept
		{
			return m_Kinds;
		}

	private:
		[[nodiscard]] bool
		HasCollision(const AssetKindDesc& desc) const;

		std::vector<AssetKindPtr>                       m_Kinds;
		core::str::unordered_str_map<const IAssetKind*> m_ByExtension;
		core::str::unordered_str_map<const IAssetKind*> m_ById;
	};
}
