#include "Windows/MaterialEditor/nodes/TextureNode.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonObject>

#include <assetlib/image_io.h>
#include <bgl/IScene.h>

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
	m_Path = path;

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
