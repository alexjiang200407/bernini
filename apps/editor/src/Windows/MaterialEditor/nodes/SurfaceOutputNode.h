#pragma once

#include <cstddef>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <filesystem>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class SurfaceTextureData;

/**
 * The sink for a material drawn by a game-defined surface, generated from reflection: one input
 * port per texture slot, one spin-box row per value with the declaration's default prefilled, and
 * the layer keys -- which are every model's, chosen per material -- as widgets of its own.
 *
 * One is registered per surface the engine reflected, named `SurfaceOutput:<surface>`. A slot
 * port carries SurfaceTextureData, never ChannelData: a surface texture is bound, not composited,
 * so a routed channel cannot be wired here at all.
 */
class SurfaceOutputNode : public MaterialSinkNode
{
	Q_OBJECT

public:
	explicit SurfaceOutputNode(bgl::SurfaceType surface);

	QString
	caption() const override;

	QString
	name() const override;

	/** The registered model name for a surface: `SurfaceOutput:<surface>`. */
	[[nodiscard]] static QString
	ModelNameFor(const std::string& surfaceName);

	unsigned int
	nPorts(QtNodes::PortType portType) const override;

	QtNodes::NodeDataType dataType(QtNodes::PortType, QtNodes::PortIndex) const override;

	std::shared_ptr<QtNodes::NodeData>
	outData(QtNodes::PortIndex) override
	{
		return nullptr;
	}

	void
	setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex port) override;

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
	CompileInto(assetlib::BMaterial& material, const std::filesystem::path& dataRoot)
		const override;

	/**
	 * The document's surface bindings and layer keys as this node's own load() state -- what seeds
	 * a board for a material that has no authored surface board. Values the document does not set
	 * are absent, so load() leaves them at the declaration's defaults.
	 */
	[[nodiscard]] static QJsonObject
	DocumentState(const assetlib::BMaterial& material);

	[[nodiscard]] const bgl::SurfaceType&
	Surface() const noexcept
	{
		return m_Surface;
	}

	/** The current value of `params.values[index]`; components past the type's stay zero. */
	[[nodiscard]] glm::vec4
	Value(size_t index) const;

	/** The path bound into texture slot `slot`, empty while nothing is wired there. */
	[[nodiscard]] QString
	BoundTexture(size_t slot) const;

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept
	{
		return m_AlphaMode;
	}

private:
	void
	SyncWidgets();

	bgl::SurfaceType m_Surface;

	// One vec4 per declared value, seeded from the defaults; authoritative over the spin boxes,
	// which round to their decimals.
	std::vector<glm::vec4> m_Values;

	// One entry per texture slot; null while nothing is wired.
	std::vector<std::shared_ptr<SurfaceTextureData>> m_Bound;

	assetlib::AlphaMode m_AlphaMode   = assetlib::AlphaMode::kOpaque;
	float               m_AlphaCutoff = 0.5f;
	bool                m_DoubleSided = true;

	QWidget*                                  m_Widget = nullptr;
	std::vector<std::vector<QDoubleSpinBox*>> m_Spins;
	QComboBox*                                m_LayerBox       = nullptr;
	QDoubleSpinBox*                           m_CutoffSpin     = nullptr;
	QCheckBox*                                m_DoubleSidedBox = nullptr;
};
