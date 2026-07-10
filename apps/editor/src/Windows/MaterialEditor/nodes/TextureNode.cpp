#include "Windows/MaterialEditor/nodes/TextureNode.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>
#include <QLabel>

#include <assetlib/image_io.h>
#include <bgl/IScene.h>

#include "Thumbnails/TexturePreviewCache.h"

namespace
{
	constexpr int c_PreviewWidgetDim = 96;
}

TextureNode::TextureNode(bgl::IScene* scene, TexturePreviewCache* previews) :
	m_Scene(scene), m_Previews(previews)
{
	if (m_Previews == nullptr)
		return;

	connect(
		m_Previews,
		&TexturePreviewCache::PreviewReady,
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
	if (!m_Texture.textureSlot)
		return nullptr;

	const auto index = static_cast<unsigned int>(port);

	if (index < c_BundleCount)
		return std::make_shared<ChannelData>(ChannelData::Bundle(m_Texture, m_Path, ArityOf(port)));

	const auto channel = static_cast<uint16_t>(index - c_BundleCount);
	return std::make_shared<ChannelData>(ChannelData::Scalar(m_Texture, m_Path, channel));
}

QString
TextureNode::portCaption(QtNodes::PortType, QtNodes::PortIndex port) const
{
	static const char* const c_Captions[c_PortCount] = { "RGBA", "RGB", "RG", "R", "G", "B", "A" };

	if (port < 0 || static_cast<unsigned int>(port) >= c_PortCount)
		return {};
	return QString::fromLatin1(c_Captions[static_cast<size_t>(port)]);
}

void
TextureNode::SetTexturePath(const QString& path)
{
	m_Path    = path;
	m_Preview = QPixmap();
	RefreshPreview();

	if (m_Scene == nullptr || path.isEmpty())
		return;

	try
	{
		m_Texture = m_Scene->AddTextureAsset(
			assetlib::loadKTX2(std::filesystem::path(path.toStdWString())));
		m_Caption = QFileInfo(path).fileName();
	}
	catch (const std::exception& e)
	{
		qWarning("TextureNode: failed to load '%s': %s", qPrintable(path), e.what());
		m_Texture = {};
		m_Caption = QStringLiteral("Texture (failed)");
		return;
	}

	if (m_Previews != nullptr)
	{
		// Decoding is asynchronous, so a texture already decoded for another node arrives now and
		// the rest land later via PreviewReady.
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
