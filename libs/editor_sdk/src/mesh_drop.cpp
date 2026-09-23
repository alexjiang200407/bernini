#include <editor_sdk/mesh_drop.h>

#include <editor_sdk/mime_files.h>
#include <editor_sdk/source_mesh.h>

#include <assetlib/codecs.h>

namespace editor
{
	namespace
	{
		QString
		FirstMesh(const QMimeData* mime)
		{
			return FirstLocalFileWithSuffix(mime, assetlib::c_MeshExtension);
		}

		QString
		FirstSource(const QMimeData* mime)
		{
			return FirstLocalFileWithSuffix(mime, assetlib::c_ImportedSourceExtension);
		}
	}

	bool
	IsMeshDrag(const QMimeData* mime)
	{
		return !FirstMesh(mime).isEmpty() || !FirstSource(mime).isEmpty();
	}

	MeshDrop
	GetMeshDroppedOn(const QMimeData* mime, const QString& dataRoot)
	{
		// A payload holding both is one model's two halves, and the container is the half that
		// reads without a project to resolve it.
		if (const QString mesh = FirstMesh(mime); !mesh.isEmpty())
			return { .mesh = mesh };

		const QString source = FirstSource(mime);
		if (source.isEmpty())
			return {};

		return { .mesh = GetSourceMesh(dataRoot, source), .source = source };
	}
}
