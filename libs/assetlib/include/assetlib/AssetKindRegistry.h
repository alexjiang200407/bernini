#pragma once

#include <assetlib/IAssetPlugin.h>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace assetlib
{
	/** Owns custom authored kinds for one project lifetime. Registration is complete before stores open. */
	class AssetKindRegistry final : public IAssetKindRegistry
	{
	public:
		void
		Add(AssetKindPtr kind) override;

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
		std::vector<AssetKindPtr>                          m_Kinds;
		std::unordered_map<std::string, const IAssetKind*> m_ByExtension;
		std::unordered_map<std::string, const IAssetKind*> m_ById;
	};
}
