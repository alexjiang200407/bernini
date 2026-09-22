#include <QObject>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <catch2/catch_test_macros.hpp>
#include <core/profiling/memory.h>
#include <cstdint>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_sdk/StampedPixmapCache.h>
#include <editor_sdk/TexturePreviewCache.h>
#include <filesystem>
#include <gamelib/ui/UiRuntime.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

#include <RmlUi/Core/RenderInterface.h>

#if defined(_WIN32)
#	include <windows.h>
#else
#	include <dlfcn.h>
#endif

namespace
{
	class StubRenderer final : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle
		CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
		{
			return 1;
		}

		void
		RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override
		{}
		void
		ReleaseGeometry(Rml::CompiledGeometryHandle) override
		{}
		Rml::TextureHandle
		LoadTexture(Rml::Vector2i&, const Rml::String&) override
		{
			return 1;
		}
		Rml::TextureHandle
		GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override
		{
			return 1;
		}
		void
		ReleaseTexture(Rml::TextureHandle) override
		{}
		void
		EnableScissorRegion(bool) override
		{}
		void
		SetScissorRegion(Rml::Rectanglei) override
		{}
	};

	class Fixture
	{
	public:
		Fixture()
		{
#if defined(_WIN32)
			m_Handle =
				LoadLibraryW(std::filesystem::path(EDITOR_SDK_FIXTURE).make_preferred().c_str());
#else
			m_Handle = dlopen(EDITOR_SDK_FIXTURE, RTLD_NOW | RTLD_LOCAL);
#endif
			if (m_Handle == nullptr)
				throw std::runtime_error("cannot load " EDITOR_SDK_FIXTURE);
		}

		template <typename Fn>
		[[nodiscard]] Fn*
		Find(const char* const name) const
		{
#if defined(_WIN32)
			auto* const symbol =
				reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_Handle), name));
#else
			auto* const symbol = dlsym(m_Handle, name);
#endif
			if (symbol == nullptr)
				throw std::runtime_error(std::string("SDK fixture exports no ") + name);
			return reinterpret_cast<Fn*>(symbol);
		}

	private:
		void* m_Handle = nullptr;
	};

	const Fixture&
	SdkFixture()
	{
		static const Fixture c_Fixture;
		return c_Fixture;
	}
}

TEST_CASE("An independent SDK plugin shares process services with its host", "[editor_plugin][sdk]")
{
	const Fixture& fixture = SdkFixture();

	CHECK(
		fixture.Find<const void*()>("BerniniEditorSdkTestDefaultLogger")() ==
		spdlog::default_logger_raw());

	const uint64_t before = core::profiling::detail::mint_allocation_id();
	const uint64_t plugin = fixture.Find<uint64_t()>("BerniniEditorSdkTestMintAllocationId")();
	const uint64_t after  = core::profiling::detail::mint_allocation_id();
	CHECK(before < plugin);
	CHECK(plugin < after);

	editor::EditorPluginPtr editorPlugin(
		fixture.Find<editor::IEditorPlugin*()>(editor::c_EditorPluginEntryPoint.data())());
	assetlib::AssetPluginPtr assetPlugin(
		fixture.Find<assetlib::IAssetPlugin*()>(assetlib::c_AssetPluginEntryPoint.data())());
	REQUIRE(editorPlugin != nullptr);
	REQUIRE(assetPlugin != nullptr);
}

TEST_CASE("An independent SDK plugin observes the host UI runtime", "[editor_plugin][sdk]")
{
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "bernini_sdk_ui";
	std::filesystem::create_directories(root);
	const assetlib::AssetStore store(root);
	StubRenderer               renderer;
	game::UiRuntime            runtime(store, renderer);
	auto                       context = runtime.CreateContext("sdk-fixture", 1, 1);

	CHECK(
		SdkFixture().Find<bool(Rml::Context*)>("BerniniEditorSdkTestUpdateContext")(
			&context->Get()));

	CHECK_FALSE(
		SdkFixture().Find<bool(const assetlib::AssetStore*, Rml::RenderInterface*)>(
			"BerniniEditorSdkTestTrySecondUiRuntime")(&store, &renderer));

	std::filesystem::remove_all(root);
}

TEST_CASE("An independent SDK plugin shares Qt support types with the host", "[editor_plugin][sdk]")
{
	const auto cache = std::unique_ptr<QObject>(
		SdkFixture().Find<QObject*()>("BerniniEditorSdkTestCreatePreviewCache")());
	REQUIRE(cache != nullptr);
	CHECK(qobject_cast<TexturePreviewCache*>(cache.get()) != nullptr);
	CHECK(qobject_cast<StampedPixmapCache*>(cache.get()) != nullptr);
	CHECK(SdkFixture().Find<bool()>("BerniniEditorSdkTestAssetPaths")());
}
