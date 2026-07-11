#pragma once

#include <QPixmap>
#include <QtNodes/NodeDelegateModel>

#include "Windows/MaterialEditor/nodes/ChannelData.h"

class QLabel;
class TexturePreviewCache;

namespace bgl
{
	class IScene;
}

class TextureNode : public QtNodes::NodeDelegateModel
{
	Q_OBJECT

public:
	// Ports 0..2 are the RGBA / RGB / RG bundles; ports 3..6 are the scalar R / G / B / A channels.
	static constexpr unsigned int c_BundleCount  = 3;
	static constexpr unsigned int c_ChannelCount = 4;
	static constexpr unsigned int c_PortCount    = c_BundleCount + c_ChannelCount;

	// `previews` may be null when the editor runs without graphics; the node then shows no image.
	TextureNode(bgl::IScene* scene, TexturePreviewCache* previews);

	QString
	caption() const override
	{
		return m_Caption;
	}

	QString
	name() const override
	{
		return QStringLiteral("Texture");
	}

	unsigned int
	nPorts(QtNodes::PortType portType) const override
	{
		return portType == QtNodes::PortType::Out ? c_PortCount : 0u;
	}

	QtNodes::NodeDataType
	dataType(QtNodes::PortType, QtNodes::PortIndex port) const override
	{
		return ChannelData::Type(ArityOf(port));
	}

	[[nodiscard]] static unsigned int
	ArityOf(QtNodes::PortIndex port) noexcept
	{
		const auto index = static_cast<unsigned int>(port);
		return index < c_BundleCount ? ChannelData::c_MaxChannels - index : 1u;
	}

	std::shared_ptr<QtNodes::NodeData>
	outData(QtNodes::PortIndex port) override;

	void
	setInData(std::shared_ptr<QtNodes::NodeData>, QtNodes::PortIndex) override
	{}

	QWidget*
	embeddedWidget() override;

	QString
	portCaption(QtNodes::PortType, QtNodes::PortIndex port) const override;

	bool
	portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const override
	{
		return true;
	}

	QJsonObject
	save() const override;
	void
	load(const QJsonObject& json) override;

	void
	SetTexturePath(const QString& path);

private:
	// Paints m_Preview into m_PreviewLabel, scaled to fit and centred. No-op before either exists.
	void
	RefreshPreview();

	bgl::IScene*            m_Scene    = nullptr;
	TexturePreviewCache*    m_Previews = nullptr;
	QString                 m_Path;
	QString                 m_Caption = QStringLiteral("Texture");
	bgl::TextureAssetHandle m_Texture;

	QLabel* m_PreviewLabel = nullptr;
	QPixmap m_Preview;
};
