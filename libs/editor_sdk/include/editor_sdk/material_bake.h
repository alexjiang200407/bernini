#pragma once
#include <QStringList>
#include <assetlib/AssetStore.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/export.h>
namespace editor
{
	/** Bake and save each material key; stops at the first failure or cancellation. */
	EDITOR_SDK_EXPORT void
	BakeMaterials(
		const assetlib::AssetStore& store,
		const QStringList&          materials,
		background::Progress&       progress);
}
