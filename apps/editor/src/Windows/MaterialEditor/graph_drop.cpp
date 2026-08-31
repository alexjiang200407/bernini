#include "Windows/MaterialEditor/graph_drop.h"

#include "util/mime_files.h"

#include <assetlib/codecs.h>

namespace editor
{
	GraphDrop
	GraphDroppedOn(const QMimeData* mime)
	{
		// A texture is a node on its own; a source is a question about which of its textures. The
		// answer that needs no asking wins where a payload carries both.
		if (const QString texture = FirstLocalFileWithSuffix(mime, assetlib::c_TextureExtension);
		    !texture.isEmpty())
			return { .texture = texture };

		return { .source = FirstLocalFileWithSuffix(mime, assetlib::c_ImportedSourceExtension) };
	}

	bool
	IsGraphDrag(const QMimeData* mime)
	{
		const GraphDrop drop = GraphDroppedOn(mime);
		return !drop.texture.isEmpty() || !drop.source.isEmpty();
	}
}
