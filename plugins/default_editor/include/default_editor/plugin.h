#pragma once
#include <array>
#include <editor_api/IEditorPlugin.h>
#include <editor_api/IEditorViewport.h>
#include <editor_sdk/environment.h>
#include <string_view>
namespace editor::defaults
{
	struct Config
	{
		ViewportDesc         materialViewport;
		EnvironmentApplyDesc materialEnvironment;
	};
	inline constexpr std::array<std::string_view, 1> c_StartupPanels{ "bernini.material" };
	[[nodiscard]] EditorPluginPtr
	CreatePlugin(Config config);
}
