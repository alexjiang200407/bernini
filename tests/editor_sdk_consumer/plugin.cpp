#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <RmlUi/Core/Context.h>
#include <assetlib/AssetStore.h>
#include <assetlib/IAssetPlugin.h>
#include <atomic>
#include <core/profiling/memory.h>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IAssetEditorFactory.h>
#include <editor_plugin_api/IEditorAction.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/TexturePreviewCache.h>
#include <editor_sdk/asset_paths.h>
#include <gamelib/ui/UiRuntime.h>
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

	class PanelFactory final : public editor::IEditorPanelFactory
	{
	public:
		editor::EditorPanel*
		Create(editor::IEditorHost& host, QWidget* parent) override
		{
			class Panel final : public editor::EditorPanel
			{
			public:
				Panel(editor::IEditorHost& host, QWidget* parent) : EditorPanel(parent)
				{
					auto* layout = new QVBoxLayout(this);
					m_Viewport   = host.CreateViewport(
						this,
						editor::ViewportDesc()
							.SetRenderScale(0.75f)
							.SetTaaReconstructionWidth(0.6f)
							.SetTaaSharpness(0.25f));
					layout->addWidget(m_Viewport);
				}
				std::vector<std::string>
				GetHeldAssets() const override
				{
					return {};
				}
				bool
				CanClose() override
				{
					return true;
				}
				void
				SetActive(bool active) override
				{
					m_Viewport->SetRenderingEnabled(active);
				}

			private:
				editor::IEditorViewport* m_Viewport = nullptr;
			};
			return new Panel(host, parent);
		}
	};
	class NotificationFactory final : public editor::IEditorPanelFactory
	{
	public:
		editor::EditorPanel*
		Create(editor::IEditorHost& host, QWidget* parent) override
		{
			class Panel final : public editor::EditorPanel
			{
			public:
				Panel(editor::IEditorHost& host, QWidget* parent) :
					EditorPanel(parent), m_Host(host)
				{
					auto* layout  = new QVBoxLayout(this);
					m_Key         = new QLineEdit("Authored/change.bfixture", this);
					m_Last        = new QLabel(this);
					auto* publish = new QPushButton("Publish", this);
					layout->addWidget(m_Key);
					layout->addWidget(m_Last);
					layout->addWidget(publish);
					connect(publish, &QPushButton::clicked, this, [this] {
						m_Host.AssetChanged(m_Key->text().toStdString());
					});
					setProperty("notificationCount", 0);
				}
				std::vector<std::string>
				GetHeldAssets() const override
				{
					return {};
				}
				bool
				CanClose() override
				{
					return true;
				}
				void
				SetActive(bool) override
				{}
				void
				OnAssetChanged(std::string_view key) override
				{
					if (property("throwOnChange").toBool())
						throw std::runtime_error("fixture notification failed");
					m_Last->setText(
						QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size())));
					setProperty("notificationCount", property("notificationCount").toInt() + 1);
				}

			private:
				editor::IEditorHost& m_Host;
				QLineEdit*           m_Key;
				QLabel*              m_Last;
			};
			return new Panel(host, parent);
		}
	};
	class ShowPanelAction final : public editor::IEditorAction
	{
	public:
		explicit ShowPanelAction(std::string id = "sample.fixture_panel") : m_Id(std::move(id)) {}
		bool
		IsEnabled(editor::IEditorHost&, std::span<const std::string>) const override
		{
			return true;
		}
		void
		Invoke(editor::IEditorHost& host, std::span<const std::string>) override
		{
			host.ShowPanel(m_Id);
		}

	private:
		std::string m_Id;
	};
	class ThrowingFactory final : public editor::IAssetEditorFactory
	{
	public:
		editor::AssetEditorPanel*
		Create(editor::IEditorHost&, QWidget* parent) override
		{
			auto* child = new QWidget(parent);
			child->setObjectName("sample.throwing_editor_child");
			throw std::runtime_error("fixture editor failed");
		}
	};
	class Plugin final : public editor::IEditorPlugin
	{
	public:
		void
		Register(editor::IEditorRegistry& registry) override
		{
			registry.AddPanel(
				editor::PanelDesc()
					.SetId("sample.fixture_panel")
					.SetTitle({ "sample.fixture", "panel", "Fixture Panel" })
					.AddFactory<PanelFactory>());
			registry.AddAction(
				editor::ActionDesc()
					.SetId("sample.show_fixture")
					.SetTitle({ "sample.fixture", "panel", "Fixture Panel" })
					.SetMenuId(std::string(editor::c_ToolsMenuId))
					.AddAction<ShowPanelAction>());
			for (const std::string id :
			     { "sample.observer_one", "sample.observer_two", "sample.observer_late" })
			{
				registry.AddPanel(
					editor::PanelDesc()
						.SetId(id)
						.SetTitle({ "sample.fixture", "observer", id.c_str() })
						.AddFactory<NotificationFactory>());
				registry.AddAction(
					editor::ActionDesc()
						.SetId(id)
						.SetTitle({ "sample.fixture", "observer", id.c_str() })
						.SetMenuId(std::string(editor::c_ToolsMenuId))
						.AddAction<ShowPanelAction>(id));
			}
			registry.AddAssetEditor(
				editor::AssetEditorDesc()
					.SetId("sample.throwing_editor")
					.SetTitle({ "sample.fixture", "editor", "Fixture Editor" })
					.AddExtension(".bfixture")
					.AddFactory<ThrowingFactory>());
		}
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

extern "C" SDK_FIXTURE_EXPORT QObject*
BerniniEditorSdkTestCreatePreviewCache()
{
	return new TexturePreviewCache();
}

extern "C" SDK_FIXTURE_EXPORT bool
BerniniEditorSdkTestAssetPaths()
{
	return editor::GetKeyUnder(
			   QStringLiteral("/project/Data"),
			   QStringLiteral("/project/Data/Authored/a.ktx2")) ==
	           QStringLiteral("Authored/a.ktx2") &&
	       editor::GetKeyUnder(
			   QStringLiteral("/project/Data"),
			   QStringLiteral("/project/other.ktx2"))
	           .isEmpty();
}
