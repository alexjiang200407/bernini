#include "AnimationEditorWindow.h"

#include "Project/Project.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AnimationEditorWindow::AnimationEditorWindow(QWidget* parent, AnimationEditorWindowDesc desc) :
	QWidget(parent)
{
	auto rt             = RenderTargetWindowDesc();
	rt.renderer         = desc.renderer;
	rt.initialInstances = desc.initialPreviewInstances;
	rt.taaEnabled       = desc.taaEnabled;
	rt.renderScale      = desc.renderScale;

	m_Preview = new AnimationPreviewWindow(this, std::move(rt), std::move(desc.previewEnv));
	m_Preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_Preview->setMinimumSize(256, 256);

	auto* openButton = new QPushButton(QStringLiteral("Open Mesh..."), this);
	connect(openButton, &QPushButton::clicked, this, &AnimationEditorWindow::OpenMeshDialog);

	m_MeshLabel = new QLabel(QStringLiteral("Drop a rigged mesh here"), this);
	m_MeshLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

	connect(m_Preview, &AnimationPreviewWindow::MeshChanged, this, [this](const QString& relPath) {
		m_MeshLabel->setText(
			relPath.isEmpty() ? QStringLiteral("Drop a rigged mesh here") : relPath);
	});

	auto* bar = new QHBoxLayout();
	bar->setContentsMargins(4, 4, 4, 4);
	bar->addWidget(openButton);
	bar->addWidget(m_MeshLabel, /*stretch*/ 1);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addLayout(bar);
	layout->addWidget(m_Preview, /*stretch*/ 1);
}

void
AnimationEditorWindow::SetDataRoot(const QString& dataRoot)
{
	m_DataRoot = dataRoot;
	m_Preview->SetDataRoot(std::filesystem::path(dataRoot.toStdWString()));
	m_Preview->Clear();
}

void
AnimationEditorWindow::SetAssets(game::AssetManager* assets)
{
	m_Preview->SetAssets(assets);
}

void
AnimationEditorWindow::OpenMeshDialog()
{
	const QString start = m_DataRoot.isEmpty() ? QString() :
	                                             m_DataRoot + QLatin1Char('/') +
	                                                 QLatin1String(Project::c_MeshesDirectoryName);

	const QString file = QFileDialog::getOpenFileName(
		this,
		QStringLiteral("Open Mesh"),
		start,
		QStringLiteral("Baked Mesh (*.bmesh)"));
	if (file.isEmpty())
		return;

	m_Preview->LoadMesh(std::filesystem::path(file.toStdWString()));
}
