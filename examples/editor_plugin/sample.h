#pragma once

#include "runtime.h"  // IWYU pragma: export
#include <editor_plugin_api/IEditorPlugin.h>

namespace sample
{
	[[nodiscard]] editor::EditorPluginPtr
	CreateEditorPlugin();
}
