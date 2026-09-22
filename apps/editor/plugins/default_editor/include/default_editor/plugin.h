#pragma once
#include <array>
#include <editor_plugin_api/IEditorPlugin.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/environment.h>
#include <string_view>
namespace editor::defaults
{
	struct Config
	{
		ViewportDesc         materialViewport;
		EnvironmentApplyDesc materialEnvironment;
		ViewportDesc         rigViewport;
		EnvironmentApplyDesc rigEnvironment;
	};
	inline constexpr std::array<std::string_view, 3> c_StartupPanels{ "bernini.material",
		                                                              "bernini.animation",
		                                                              "bernini.blend_space" };
	[[nodiscard]] EditorPluginPtr
	CreatePlugin(Config config);
}
