#pragma once

#include <assetlib/IAssetPlugin.h>
#include <editor_api/IEditorPlugin.h>
#include <memory>

namespace sample
{
	[[nodiscard]] std::unique_ptr<assetlib::IAssetPlugin>
	CreateAssetPlugin();

	[[nodiscard]] std::unique_ptr<editor::IEditorPlugin>
	CreateEditorPlugin();
}
