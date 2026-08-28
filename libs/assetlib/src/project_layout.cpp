#include <assetlib/project_layout.h>

#include <core/err/util.h>

#include "ref_paths.h"

namespace assetlib
{
	namespace
	{
		std::string_view
		nameOf(const AssetOrigin origin) noexcept
		{
			return origin == AssetOrigin::kDerived ? c_DerivedDirectoryName :
			                                         c_AuthoredDirectoryName;
		}
	}

	std::optional<AssetOrigin>
	originOf(const std::string_view key) noexcept
	{
		const std::string normalized = normalizeRef(key);

		if (isUnder(normalized, c_AuthoredDirectoryName))
			return AssetOrigin::kAuthored;

		if (isUnder(normalized, c_DerivedDirectoryName))
			return AssetOrigin::kDerived;

		return std::nullopt;
	}

	void
	requireOrigin(const std::string_view key, const AssetOrigin origin, const std::string_view what)
	{
		core::throw_runtime_error_if(
			originOf(key) != origin,
			"{}: {} belongs under {}/, so '{}' is not a place it can be written",
			what,
			origin == AssetOrigin::kDerived ? "a derived container" : "an authored document",
			nameOf(origin),
			key);
	}
}
