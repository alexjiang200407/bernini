#include "Windows/AnimationEditor/AnimationEditorWindow.h"
#include "Windows/BlendSpaceEditor/BlendSpaceEditorWindow.h"
#include "Windows/GrassEditor/GrassEditorWindow.h"
#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include <assetlib/codecs.h>
#include <default_editor/plugin.h>
#include <editor_plugin_api/EditorPanel.h>
#include <editor_plugin_api/IAssetEditorFactory.h>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorPanelFactory.h>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorRegistry.h>
#include <memory>
#include <qwidget.h>
#include <string>
#include <utility>
namespace editor::defaults
{
	namespace
	{
		class MaterialFactory final : public IEditorPanelFactory
		{
		public:
			explicit MaterialFactory(Config config) : m_Config(std::move(config)) {}
			EditorPanel*
			Create(IEditorHost& host, QWidget* parent) override
			{
				return new MaterialEditorWindow(
					host,
					parent,
					{ m_Config.materialViewport, m_Config.materialEnvironment });
			}

		private:
			Config m_Config;
		};
		class AnimationFactory final : public IEditorPanelFactory
		{
		public:
			explicit AnimationFactory(Config config) : m_Config(std::move(config)) {}
			EditorPanel*
			Create(IEditorHost& host, QWidget* parent) override
			{
				return new AnimationEditorWindow(
					host,
					parent,
					{ m_Config.rigViewport, m_Config.rigEnvironment });
			}

		private:
			Config m_Config;
		};
		class BlendSpaceFactory final : public IAssetEditorFactory
		{
		public:
			explicit BlendSpaceFactory(Config config) : m_Config(std::move(config)) {}
			AssetEditorPanel*
			Create(IEditorHost& host, QWidget* parent) override
			{
				return new BlendSpaceEditorWindow(
					host,
					parent,
					m_Config.rigViewport,
					m_Config.rigEnvironment);
			}

		private:
			Config m_Config;
		};
		class GrassFactory final : public IAssetEditorFactory
		{
		public:
			explicit GrassFactory(Config config) : m_Config(std::move(config)) {}
			AssetEditorPanel*
			Create(IEditorHost& host, QWidget* parent) override
			{
				return new GrassEditorWindow(
					host,
					parent,
					m_Config.materialViewport,
					m_Config.materialEnvironment);
			}

		private:
			Config m_Config;
		};
		class Plugin final : public IEditorPlugin
		{
		public:
			explicit Plugin(Config config) : m_Config(std::move(config)) {}
			void
			Register(IEditorRegistry& registry) override
			{
				registry.AddPanel(
					PanelDesc()
						.SetId("bernini.material")
						.SetTitle({ "bernini.material", "title", "Material Editor" })
						.AddFactory<MaterialFactory>(m_Config));
				registry.AddPanel(
					PanelDesc()
						.SetId("bernini.animation")
						.SetTitle({ "bernini.animation", "title", "Animation Editor" })
						.AddFactory<AnimationFactory>(m_Config));
				registry.AddAssetEditor(
					AssetEditorDesc()
						.SetId("bernini.blend_space")
						.SetTitle({ "bernini.blend_space", "title", "Blend Space Editor" })
						.AddExtension(".bblend")
						.AddFactory<BlendSpaceFactory>(m_Config));
				registry.AddAssetEditor(
					AssetEditorDesc()
						.SetId("bernini.grass")
						.SetTitle({ "bernini.grass", "title", "Grass Editor" })
						.AddExtension(std::string(assetlib::c_GrassExtension))
						.AddFactory<GrassFactory>(m_Config));
			}

		private:
			Config m_Config;
		};
	}
	EditorPluginPtr
	CreatePlugin(Config config)
	{
		return std::make_unique<Plugin>(std::move(config));
	}
}
