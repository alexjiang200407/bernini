#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"

#include <QApplication>
#include <QEvent>
#include <QWidget>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <filesystem>
#include <memory>
#include <qlogging.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>

#include "Windows/MaterialEditor/material_graph.h"
#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include <QtNodes/internal/NodeData.hpp>
#include <assetlib_structs/BMaterial.h>

QString
MaterialSinkNode::Uv1OcclusionCaption()
{
	return QStringLiteral("Occlusion (UV1)");
}

void
MaterialSinkNode::SetUv1Occlusion(std::shared_ptr<QtNodes::NodeData> data)
{
	m_Uv1Occlusion = std::dynamic_pointer_cast<ChannelData>(data);
	Q_EMIT Changed();
}

void
MaterialSinkNode::CompileUv1Occlusion(
	assetlib::BMaterial&         material,
	const std::filesystem::path& dataRoot) const
{
	const ChannelData::Route wired = Uv1OcclusionRoute();

	// The map is sampled whole and its red read, so a wire from another channel names the file and
	// nothing else.
	if (!wired.path.isEmpty() && wired.channel != 0)
	{
		qWarning(
			"MaterialEditor: the UV1 occlusion map reads red; the %c channel wired from '%s' is "
			"not the one sampled",
			"rgba"[wired.channel & 3u],
			qPrintable(wired.path));
	}

	material.uv1OcclusionTexture = Rebase(wired.path, dataRoot, true).toStdString();
}

void
MaterialSinkNode::WatchEmbeddedWidget(QWidget* widget)
{
	m_WatchedWidget = widget;
	widget->installEventFilter(this);
}

QWidget*
MaterialSinkNode::DialogOwnerFor(QWidget* embedded)
{
	QWidget* owner = embedded != nullptr ? embedded->window() : nullptr;
	if (owner == nullptr || owner->graphicsProxyWidget() != nullptr)
		owner = QApplication::activeWindow();

	return owner;
}

bool
MaterialSinkNode::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_WatchedWidget)
	{
		// A row shown or hidden invalidates the layout without resizing the widget -- nothing
		// lays the proxied widget out from outside, so a shrunk size hint would otherwise leave
		// a gap. Adopting the hint is what turns it into the resize below.
		if (event->type() == QEvent::LayoutRequest)
			m_WatchedWidget->adjustSize();

		// No recursion: the re-measure repositions the proxy and repaints, and never resizes the
		// widget back.
		if (event->type() == QEvent::Resize)
			Q_EMIT requestNodeUpdate();
	}

	return QtNodes::NodeDelegateModel::eventFilter(watched, event);
}
