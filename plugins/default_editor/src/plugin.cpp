#include "Windows/MaterialEditor/MaterialEditorWindow.h"
#include <default_editor/plugin.h>
#include <editor_api/IEditorPanelFactory.h>
#include <editor_api/IEditorRegistry.h>
#include <memory>
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
