#pragma once

#include <QtNodes/NodeDelegateModel>

#include <assetlib_structs/BMaterial.h>
#include <filesystem>
#include <memory>
#include <qstring.h>
#include <qtmetamacros.h>

#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <QtNodes/internal/NodeDelegateModel.hpp>

class QEvent;
class QObject;
class QWidget;

/**
 * A material graph's sink: the node the material compiles from.
 *
 * MaterialGraphModel finds, guards and swaps the sink through this type, so a graph holds exactly
 * one of these whatever kind it is. A sink owns what the material says -- the shading model, that
 * model's parameters, and the layer keys -- and nothing else about the document.
 */
class MaterialSinkNode : public QtNodes::NodeDelegateModel
{
	Q_OBJECT

public:
	/**
	 * Writes what this sink authors into `material`: the shading model, its parameters and the
	 * layer keys. Texture references are stored relative to `dataRoot`, like every asset
	 * reference. The rest of the document -- the name, the serialized board, the baked state --
	 * is the caller's.
	 */
	virtual void
	CompileInto(assetlib::BMaterial& material, const std::filesystem::path& dataRoot) const = 0;

	/**
	 * The input the UV1 occlusion map is wired into: every sink has one, after all the ports its
	 * own model declares, so a board saved before it existed keeps every connection it had.
	 */
	[[nodiscard]] QtNodes::PortIndex
	Uv1OcclusionPort() const
	{
		return static_cast<QtNodes::PortIndex>(ModelPortCount());
	}

	/** Whether anything is wired into Uv1OcclusionPort. */
	[[nodiscard]] bool
	HasUv1Occlusion() const noexcept
	{
		return m_Uv1Occlusion != nullptr;
	}

	/** What is wired into Uv1OcclusionPort -- its file and its upload -- or an empty route. */
	[[nodiscard]] ChannelData::Route
	Uv1OcclusionRoute() const noexcept
	{
		return m_Uv1Occlusion != nullptr ? m_Uv1Occlusion->At(0) : ChannelData::Route{};
	}

protected:
	// The input ports the sink's own model declares, which Uv1OcclusionPort follows.
	[[nodiscard]] virtual unsigned int
	ModelPortCount() const = 0;

	[[nodiscard]] bool
	IsUv1OcclusionPort(QtNodes::PortIndex port) const
	{
		return port == Uv1OcclusionPort();
	}

	[[nodiscard]] static QtNodes::NodeDataType
	Uv1OcclusionType()
	{
		return ChannelData::Type(1);
	}

	[[nodiscard]] static QString
	Uv1OcclusionCaption();

	// Takes the payload QtNodes pushes into Uv1OcclusionPort, null on a disconnect.
	void
	SetUv1Occlusion(std::shared_ptr<QtNodes::NodeData> data);

	// Writes the wired map into `material.uv1OcclusionTexture`, relative to `dataRoot`, or clears it.
	void
	CompileUv1Occlusion(assetlib::BMaterial& material, const std::filesystem::path& dataRoot) const;

	/**
	 * Re-measures the node whenever `widget` resizes. QtNodes reads the embedded widget's size
	 * only when the node is created, so a widget that settles on first show -- or a form row
	 * shown or hidden later -- would otherwise overflow the frame or leave a gap. Call it once
	 * from embeddedWidget() on the widget it built.
	 */
	void
	WatchEmbeddedWidget(QWidget* widget);

	/**
	 * The widget a sink's modal dialog is parented to. NOT `embedded` itself: an embedded widget
	 * is reparented into a QGraphicsProxyWidget, and Qt embeds a proxied widget's child windows
	 * into the graphics scene too -- the dialog's real window comes up blank while its contents
	 * are painted onto the node canvas, and the scene is left with a stray proxy afterwards. The
	 * embedded widget's top-level window carries modality and taskbar grouping instead.
	 */
	[[nodiscard]] static QWidget*
	DialogOwnerFor(QWidget* embedded);

	bool
	eventFilter(QObject* watched, QEvent* event) override;

private:
	QWidget* m_WatchedWidget = nullptr;

	std::shared_ptr<ChannelData> m_Uv1Occlusion;

Q_SIGNALS:
	// Something the compiled material depends on changed; the window recompiles the preview on it.
	void
	Changed();
};
