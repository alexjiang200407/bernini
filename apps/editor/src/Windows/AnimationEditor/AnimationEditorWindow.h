#pragma once

#include <QWidget>

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"

class QLabel;

struct AnimationEditorWindowDesc
{
	Renderer*                    renderer                = nullptr;
	uint32_t                     initialPreviewInstances = 16;
	bool                         taaEnabled              = true;
	float                        renderScale             = 1.0f;
	editor::EnvironmentApplyDesc previewEnv;
};

/**
 * The Animation panel: the preview viewport under a thin bar naming the opened mesh.
 */
class AnimationEditorWindow : public QWidget
{
	Q_OBJECT

public:
	explicit AnimationEditorWindow(QWidget* parent = nullptr, AnimationEditorWindowDesc desc = {});

	/** The open project's Data directory; clears the preview, since its mesh belonged to the last one. */
	void
	SetDataRoot(const QString& dataRoot);

	/** Forwarded to the preview -- nullptr releases everything it holds. */
	void
	SetAssets(game::AssetManager* assets);

private:
	void
	OpenMeshDialog();

	AnimationPreviewWindow* m_Preview   = nullptr;
	QLabel*                 m_MeshLabel = nullptr;
	QString                 m_DataRoot;
};
