#include <assetlib/AssetKindRegistry.h>

#include <assetlib/AssetCodec.h>
#include <assetlib/IAssetPlugin.h>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
	bool
	validId(std::string_view value) noexcept
	{
		if (value.empty())
			return false;
		for (const unsigned char c : value)
			if (!(std::isalnum(c) != 0 || c == '_' || c == '.'))
				return false;
		return true;
	}

	bool
	validExtension(std::string_view value) noexcept
	{
		if (value.size() < 2 || value.front() != '.')
			return false;
		for (const unsigned char c : value.substr(1))
			if (!(std::islower(c) != 0 || std::isdigit(c) != 0 || c == '_'))
				return false;
		return true;
	}
}

namespace assetlib
{
	void
	AssetKindRegistry::Add(AssetKindPtr kind)
	{
		if (!kind)
			throw std::runtime_error("assetlib: cannot register a null asset kind");
		const auto& desc = kind->GetDesc();
		if (!validId(desc.id) || !validExtension(desc.extension))
			throw std::runtime_error("assetlib: invalid asset kind descriptor");
		if (containerKindForExtension(desc.extension).has_value() ||
		    foreignKindForExtension(desc.extension).has_value() ||
		    FindByExtension(desc.extension) || FindById(desc.id))
			throw std::runtime_error("assetlib: asset kind descriptor collides");
		m_Kinds.push_back(std::move(kind));
	}

	const IAssetKind*
	AssetKindRegistry::FindByExtension(std::string_view extension) const noexcept
	{
		for (const auto& kind : m_Kinds)
			if (kind->GetDesc().extension == extension)
				return kind.get();
		return nullptr;
	}

	const IAssetKind*
	AssetKindRegistry::FindById(std::string_view id) const noexcept
	{
		for (const auto& kind : m_Kinds)
			if (kind->GetDesc().id == id)
				return kind.get();
		return nullptr;
	}
}
