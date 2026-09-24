#include "Windows/MaterialEditor/nodes/TextureNode.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QLabel>

#include <assetlib/image_io.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/IScene.h>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <qhashfunctions.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpixmap.h>
#include <qtmetamacros.h>
#include <qwidget.h>
#include <utility>

#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/SurfaceTextureData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <editor_plugin_api/IEditorHost.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/localize.h>
#include <editor_sdk/StampedPixmapCache.h>
#include <editor_sdk/TexturePreviewCache.h>

namespace
{
	constexpr int c_PreviewWidgetDim = 96;
}

TextureNode::TextureNode(
	const editor::ILanguageResolver& language,
	editor::IEditorHost*             host,
	TexturePreviewCache*             previews) :
	m_Language(language), m_Host(host), m_Previews(previews),
	m_Caption(editor::Localize(m_Language, "bernini.material_nodes.texture_caption", "Texture"))
{
	if (m_Previews == nullptr)
		return;

	connect(
		m_Previews,
		&StampedPixmapCache::Ready,
		this,
		[this](const QString& path, const QPixmap& preview) {
			// One decode notifies every node; several nodes commonly share a texture.
			if (path != m_Path)
				return;

			m_Preview = preview;
			RefreshPreview();
		});
}

QWidget*
TextureNode::embeddedWidget()
{
	if (m_PreviewLabel != nullptr)
		return m_PreviewLabel;

	m_PreviewLabel = new QLabel();
	m_PreviewLabel->setFixedSize(c_PreviewWidgetDim, c_PreviewWidgetDim);
	m_PreviewLabel->setAlignment(Qt::AlignCenter);
	m_PreviewLabel->setStyleSheet("background: #2b2b2b; border: 1px solid #555;");

	// The decode may have landed before QtNodes asked for the widget.
	RefreshPreview();
	return m_PreviewLabel;
}

void
TextureNode::RefreshPreview()
{
	if (m_PreviewLabel == nullptr)
		return;

	if (m_Preview.isNull())
	{
		m_PreviewLabel->clear();
		return;
	}

	m_PreviewLabel->setPixmap(
		m_Preview.scaled(m_PreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

std::shared_ptr<QtNodes::NodeData>
TextureNode::outData(QtNodes::PortIndex port)
{
	if (port < 0 || static_cast<unsigned int>(port) >= c_PortCount)
		return nullptr;

	// A route is a (file, channel) pair, so the path decides whether this node feeds one -- not the
	// handle, which is null when there is no device to load the file with. A null handle resolves to
	// the same default an unrouted channel gets, so gating on it would drop the route instead.
	if (m_Path.isEmpty())
		return nullptr;

	const auto index = static_cast<unsigned int>(port);

	if (index == c_TexturePort)
		return std::make_shared<SurfaceTextureData>(m_Texture, m_Path);

	if (index < c_BundleCount)
		return std::make_shared<ChannelData>(ChannelData::Bundle(m_Texture, m_Path, ArityOf(port)));

	const auto channel = static_cast<uint16_t>(index - c_BundleCount);
	return std::make_shared<ChannelData>(ChannelData::Scalar(m_Texture, m_Path, channel));
}

QString
TextureNode::portCaption(QtNodes::PortType, QtNodes::PortIndex port) const
{
	// Channel bundle/scalar codes are universal shorthand (as in any image editor), not sentences,
	// so only the whole-texture port's caption is a translated word.
	static const char* const c_ChannelCaptions[c_PortCount - 1] = { "RGBA", "RGB", "RG", "R",
		                                                            "G",    "B",   "A" };

	if (port < 0 || static_cast<unsigned int>(port) >= c_PortCount)
		return {};

	const auto index = static_cast<size_t>(port);
	if (index == c_TexturePort)
		return editor::Localize(
			m_Language,
			"bernini.material_nodes.texture_port_caption",
			"Texture");

	return QString::fromLatin1(c_ChannelCaptions[index]);
}

void
TextureNode::SetTexturePath(const QString& path)
{
	m_Path    = path;
	m_Preview = QPixmap();
	RefreshPreview();

	if (m_Host == nullptr || path.isEmpty())
		return;

	try
	{
		// Decoded here, so only the upload costs the render thread a round-trip.
		auto image = assetlib::loadKTX2(std::filesystem::path(path.toStdWString()));

		m_Host->InvokeRender([&](editor::RenderContext& context) {
			m_Texture = context.scene.AddTextureAsset(std::move(image));
		});
		m_Caption = QFileInfo(path).fileName();
	}
	catch (const std::exception& e)
	{
		qWarning("TextureNode: failed to load '%s': %s", qPrintable(path), e.what());
		m_Texture = {};
		m_Caption = editor::Localize(
			m_Language,
			"bernini.material_nodes.texture_failed_caption",
			"Texture (failed)");
		return;
	}

	if (m_Previews != nullptr)
	{
		// Decoding is asynchronous, so a texture already decoded for another node arrives now and
		// the rest land later via Ready.
		m_Preview = m_Previews->Lookup(path);
		if (m_Preview.isNull())
			m_Previews->Request(path);
		else
			RefreshPreview();
	}

	for (unsigned int port = 0; port < c_PortCount; ++port)
		Q_EMIT dataUpdated(static_cast<QtNodes::PortIndex>(port));
}

QJsonObject
TextureNode::save() const
{
	QJsonObject json = NodeDelegateModel::save();
	json["texture"]  = m_Path;
	return json;
}

void
TextureNode::load(const QJsonObject& json)
{
	SetTexturePath(json["texture"].toString());
}
