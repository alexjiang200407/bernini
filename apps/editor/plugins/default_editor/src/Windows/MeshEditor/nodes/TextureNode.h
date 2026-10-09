#pragma once

#include <QPixmap>
#include <QtNodes/NodeDelegateModel>
#include <memory>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include "Windows/MeshEditor/nodes/ChannelData.h"
#include "Windows/MeshEditor/nodes/SurfaceTextureData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>
#include <bgl/types/TextureAssetHandle.h>
#include <editor_plugin_api/ILanguageResolver.h>

class QLabel;
class TexturePreviewCache;
class TextureUploads;

class TextureNode : public QtNodes::NodeDelegateModel
{
	Q_OBJECT

public:
	// Ports 0..2 are the RGBA / RGB / RG bundles; ports 3..6 are the scalar R / G / B / A channels;
	// port 7 is the whole texture, for a surface slot -- bound, never routed.
	static constexpr unsigned int c_BundleCount  = 3;
	static constexpr unsigned int c_ChannelCount = 4;
	static constexpr unsigned int c_TexturePort  = c_BundleCount + c_ChannelCount;
	static constexpr unsigned int c_PortCount    = c_TexturePort + 1;

	// `uploads` and `previews` may be null when the editor runs without graphics; the node then
	// uploads nothing and shows no image. Each that is not, and `language`, must outlive the node.
	TextureNode(
		const editor::ILanguageResolver& language,
		TextureUploads*                  uploads,
		TexturePreviewCache*             previews);

	~TextureNode() override;

	TextureNode(const TextureNode&) = delete;

	TextureNode&
	operator=(const TextureNode&) = delete;

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
		if (static_cast<unsigned int>(port) == c_TexturePort)
			return SurfaceTextureData::Type();
		return ChannelData::Type(ArityOf(port));
	}

	// The channel width of a bundle or scalar port; the whole-texture port never asks.
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

	/**
	 * Points the node at `path`. Its upload arrives later -- the ports carry the path at once, and
	 * a null texture until then, which the surface reads as its default.
	 */
	void
	SetTexturePath(const QString& path);

	[[nodiscard]] const QString&
	TexturePath() const noexcept
	{
		return m_Path;
	}

	/**
	 * Takes the upload of its path as TextureUploads now has it, and tells the ports and the
	 * caption. The panel calls it when the upload settles: an upload arriving changes what the
	 * preview draws and never what the material says, which only the panel can keep from reading as
	 * an edit.
	 */
	void
	TakeUpload();

private:
	// Paints m_Preview into m_PreviewLabel, scaled to fit and centred. No-op before either exists.
	void
	RefreshPreview();

	const editor::ILanguageResolver& m_Language;
	TextureUploads*                  m_Uploads  = nullptr;
	TexturePreviewCache*             m_Previews = nullptr;
	QString                          m_Path;
	QString                          m_Caption;
	bgl::TextureAssetHandle          m_Texture;

	QLabel* m_PreviewLabel = nullptr;
	QPixmap m_Preview;
};
