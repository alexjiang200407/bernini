#include <RmlUi/Core/Context.h>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <core/profiling/memory.h>
#include <cstdint>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/IEditorRegistry.h>
#include <gamelib/ui/UiRuntime.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

#if defined(_WIN32)
#	define SDK_FIXTURE_EXPORT __declspec(dllexport)
#else
#	define SDK_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
	class Plugin final : public editor::IEditorPlugin
	{
	public:
		void
		Register(editor::IEditorRegistry&) override
		{}
	};

	class AssetPlugin final : public assetlib::IAssetPlugin
	{
	public:
		void
		RegisterKinds(assetlib::IAssetKindRegistry&) override
		{}
	};
}

extern "C" SDK_FIXTURE_EXPORT editor::IEditorPlugin*
							  BerniniCreateEditorPlugin()
							  {
								  return new Plugin();
							  }

extern "C" SDK_FIXTURE_EXPORT assetlib::IAssetPlugin*
							  BerniniCreateAssetPlugin()
							  {
								  return new AssetPlugin();
							  }

extern "C" SDK_FIXTURE_EXPORT const void*
EditorSdkDefaultLogger()
{
	return spdlog::default_logger_raw();
}

extern "C" SDK_FIXTURE_EXPORT uint64_t
EditorSdkMintAllocationId()
{
	return core::profiling::detail::mint_allocation_id();
}

extern "C" SDK_FIXTURE_EXPORT bool
EditorSdkUpdateContext(Rml::Context* const context)
{
	return context->Update();
}

extern "C" SDK_FIXTURE_EXPORT bool
EditorSdkTrySecondUiRuntime(
	const assetlib::AssetStore* const store,
	Rml::RenderInterface* const       renderer)
{
	try
	{
		game::UiRuntime runtime(*store, *renderer);
		return true;
	}
	catch (const std::runtime_error&)
	{
		return false;
	}
}
