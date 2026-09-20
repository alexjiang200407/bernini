#include <assetlib/AssetKindRegistry.h>

#include <assetlib/AssetCodec.h>
#include <assetlib/IAssetPlugin.h>
#include <cctype>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	bool
	validId(std::string_view value) noexcept
	{
		if (value.empty())
			return false;
		for (const char raw : value)
		{
			const unsigned char c = static_cast<unsigned char>(raw);
			if (!(std::isalnum(c) != 0 || c == '_' || c == '.'))
				return false;
		}
		return true;
	}

	bool
	validExtension(std::string_view value) noexcept
	{
		if (value.size() < 2 || value.front() != '.')
			return false;
		for (const char raw : value.substr(1))
		{
			const unsigned char c = static_cast<unsigned char>(raw);
			if (!(std::islower(c) != 0 || std::isdigit(c) != 0 || c == '_'))
				return false;
		}
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
		if (HasCollision(desc))
			throw std::runtime_error("assetlib: asset kind descriptor collides");

		const IAssetKind* added = kind.get();
		m_Kinds.push_back(std::move(kind));
		try
		{
			m_ByExtension.emplace(desc.extension, added);
			try
			{
				m_ById.emplace(desc.id, added);
			}
			catch (...)
			{
				m_ByExtension.erase(desc.extension);
				throw;
			}
		}
		catch (...)
		{
			m_Kinds.pop_back();
			throw;
		}
	}

	void
	AssetKindRegistry::Merge(AssetKindRegistry&& other)
	{
		for (const AssetKindPtr& kind : other.m_Kinds)
		{
			if (kind == nullptr || HasCollision(kind->GetDesc()))
				throw std::runtime_error("assetlib: asset kind descriptor collides");
		}

		auto byExtension = m_ByExtension;
		auto byId        = m_ById;
		for (const AssetKindPtr& kind : other.m_Kinds)
		{
			byExtension.emplace(kind->GetDesc().extension, kind.get());
			byId.emplace(kind->GetDesc().id, kind.get());
		}

		std::vector<AssetKindPtr> kinds;
		kinds.reserve(m_Kinds.size() + other.m_Kinds.size());
		for (AssetKindPtr& kind : m_Kinds) kinds.push_back(std::move(kind));
		for (AssetKindPtr& kind : other.m_Kinds) kinds.push_back(std::move(kind));

		m_Kinds.swap(kinds);
		m_ByExtension.swap(byExtension);
		m_ById.swap(byId);
		other.m_Kinds.clear();
		other.m_ByExtension.clear();
		other.m_ById.clear();
	}

	bool
	AssetKindRegistry::HasCollision(const AssetKindDesc& desc) const
	{
		return containerKindForExtension(desc.extension).has_value() ||
		       foreignKindForExtension(desc.extension).has_value() ||
		       FindByExtension(desc.extension) != nullptr || FindById(desc.id) != nullptr;
	}

	const IAssetKind*
	AssetKindRegistry::FindByExtension(std::string_view extension) const noexcept
	{
		const auto found = m_ByExtension.find(extension);
		return found == m_ByExtension.end() ? nullptr : found->second;
	}

	const IAssetKind*
	AssetKindRegistry::FindById(std::string_view id) const noexcept
	{
		const auto found = m_ById.find(id);
		return found == m_ById.end() ? nullptr : found->second;
	}
}
