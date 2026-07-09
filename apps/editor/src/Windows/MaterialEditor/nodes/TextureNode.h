#pragma once

#include <QtNodes/NodeDelegateModel>

#include "Windows/MaterialEditor/nodes/ChannelData.h"

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

	explicit TextureNode(bgl::IScene* scene) : m_Scene(scene) {}

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
	embeddedWidget() override
	{
		return nullptr;
	}

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
	bgl::IScene*            m_Scene = nullptr;
	QString                 m_Path;
	QString                 m_Caption = QStringLiteral("Texture");
	bgl::TextureAssetHandle m_Texture;
};
