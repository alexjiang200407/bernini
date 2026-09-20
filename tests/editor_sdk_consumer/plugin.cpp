#include <RmlUi/Core/Context.h>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <atomic>
#include <core/profiling/memory.h>
#include <cstddef>
#include <cstdint>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/IEditorRegistry.h>
#include <gamelib/ui/UiRuntime.h>
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#	define SDK_FIXTURE_EXPORT __declspec(dllexport)
#else
#	define SDK_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

namespace
{
	std::atomic_uint32_t g_AssetFactoryCalls  = 0;
	std::atomic_uint32_t g_EditorFactoryCalls = 0;

	class FixtureKind final : public assetlib::IAssetKind
	{
	public:
		const assetlib::AssetKindDesc&
		GetDesc() const noexcept override
		{
			return m_Desc;
		}

		std::vector<assetlib::DocumentReference>
		ReadReferences(std::span<const std::byte>) const override
		{
			return {};
		}

		std::vector<std::byte>
		RewriteReferences(
			std::span<const std::byte> bytes,
			std::span<const assetlib::DocumentReference>) const override
		{
			return { bytes.begin(), bytes.end() };
		}

		std::vector<std::byte>
		Migrate(std::span<const std::byte> bytes) const override
		{
			return { bytes.begin(), bytes.end() };
		}

	private:
		assetlib::AssetKindDesc m_Desc{ "sample.fixture", ".bfixture", true };
	};

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
		RegisterKinds(assetlib::IAssetKindRegistry& registry) override
		{
			registry.Add(std::make_unique<FixtureKind>());
		}
	};
}

extern "C" SDK_FIXTURE_EXPORT editor::IEditorPlugin*
							  BerniniCreateEditorPlugin()
							  {
								  ++g_EditorFactoryCalls;
								  return std::make_unique<Plugin>().release();
							  }

extern "C" SDK_FIXTURE_EXPORT assetlib::IAssetPlugin*
							  BerniniCreateAssetPlugin()
							  {
								  ++g_AssetFactoryCalls;
								  return std::make_unique<AssetPlugin>().release();
							  }

extern "C" SDK_FIXTURE_EXPORT uint32_t
BerniniEditorSdkTestAssetFactoryCalls()
{
	return g_AssetFactoryCalls.load();
}

extern "C" SDK_FIXTURE_EXPORT uint32_t
BerniniEditorSdkTestEditorFactoryCalls()
{
	return g_EditorFactoryCalls.load();
}

extern "C" SDK_FIXTURE_EXPORT const void*
BerniniEditorSdkTestDefaultLogger()
{
	return spdlog::default_logger_raw();
}

extern "C" SDK_FIXTURE_EXPORT uint64_t
BerniniEditorSdkTestMintAllocationId()
{
	return core::profiling::detail::mint_allocation_id();
}

extern "C" SDK_FIXTURE_EXPORT bool
BerniniEditorSdkTestUpdateContext(Rml::Context* const context)
{
	return context->Update();
}

extern "C" SDK_FIXTURE_EXPORT bool
BerniniEditorSdkTestTrySecondUiRuntime(
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
