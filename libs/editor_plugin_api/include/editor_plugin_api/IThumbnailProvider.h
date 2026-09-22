#pragma once
#include <assetlib/AssetStore.h>
#include <concepts>
#include <editor_plugin_api/Thumbnail.h>
#include <memory>
#include <string_view>

namespace editor
{
	// Registry-owned; call arguments are borrowed and must not be retained.
	class IThumbnailProvider
	{
	public:
		virtual ~IThumbnailProvider()                 = default;
		IThumbnailProvider(const IThumbnailProvider&) = delete;
		IThumbnailProvider(IThumbnailProvider&&)      = delete;
		IThumbnailProvider&
		operator=(const IThumbnailProvider&) = delete;
		IThumbnailProvider&
		operator=(IThumbnailProvider&&) = delete;

		// May run concurrently; implementations must not access widgets.
		virtual Thumbnail
		Describe(const assetlib::AssetStore& store, std::string_view key) const = 0;

	protected:
		IThumbnailProvider() = default;
	};
	using ThumbnailProviderPtr = std::unique_ptr<IThumbnailProvider>;

	template <typename T, typename... Args>
	concept ThumbnailProviderFor =
		std::derived_from<T, IThumbnailProvider> && std::constructible_from<T, Args...>;

}
