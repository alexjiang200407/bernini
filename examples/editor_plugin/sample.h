#pragma once

#include <assetlib/IAssetPlugin.h>
#include <editor_api/IEditorPlugin.h>

namespace sample
{
	[[nodiscard]] assetlib::AssetPluginPtr
	CreateAssetPlugin();

	[[nodiscard]] editor::EditorPluginPtr
	CreateEditorPlugin();
}
