#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>
#include <bgl/TextureAssetHandle.h>
#include <filesystem>
#include <qjsonobject.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qwidget.h>

#include "Windows/MaterialEditor/nodes/ChannelData.h"
#include "Windows/MaterialEditor/nodes/MaterialSinkNode.h"
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeData.hpp>
#include <array>

class QDoubleSpinBox;
class QPushButton;
class SurfaceTextureData;

/**
 * The sink for a material drawn by a game-defined surface, generated from reflection: one input
 * port per texture slot, one spin-box row per value with the declaration's default prefilled.
 * The layer keys are its state too, edited from the properties panel (ADR-9).
 *
 * One is registered per surface the engine reflected, named `SurfaceOutput:<surface>`. A slot's
 * whole-texture port carries SurfaceTextureData; a *data* slot also offers one channel port per
 * component, carrying single-channel ChannelData, and the two kinds are mutually exclusive per
 * slot (ADR-7) -- the model asks PortAccepts before offering a wire. Colour, normal and coverage
 * slots stay whole-bound.
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

	/**
	 * Writes `params.values[index]` -- what the colour picker lands. Components past the type's
	 * are zeroed, the row's widgets follow, and Changed is emitted on an actual change.
	 */
	void
	SetValue(size_t index, const glm::vec4& value);

	/** How a port index maps onto the declared slots: every slot has a whole-texture port, and a
	 *  data slot puts one channel port per component after its own. */
	struct PortRef
	{
		size_t   slot      = SIZE_MAX;  // == textures.size() when the index is out of range
		bool     whole     = true;
		uint32_t component = 0;
	};

	[[nodiscard]] PortRef
	ResolvePort(QtNodes::PortIndex port) const;

	/** The whole-texture port of `slot`. */
	[[nodiscard]] unsigned int
	WholePortFor(size_t slot) const;

	/** The `component`-th channel port of data slot `slot`. */
	[[nodiscard]] unsigned int
	ChannelPortFor(size_t slot, uint32_t component) const;

	/** Whether anything is wired into `slot`'s channel ports. */
	[[nodiscard]] bool
	SlotIsRouted(size_t slot) const;

	/**
	 * Whether `port` may take a wire: a slot is bound whole or composited from routes, never both
	 * (ADR-7), so each kind refuses while the other is wired. The model asks on every offered
	 * connection.
	 */
	[[nodiscard]] bool
	PortAccepts(QtNodes::PortIndex port) const;

	/** The route wired into `slot`'s `component` channel port; empty path when unwired. */
	[[nodiscard]] ChannelData::Route
	RouteFor(size_t slot, uint32_t component) const;

	/** The path bound into texture slot `slot`, empty while nothing is wired there. */
	[[nodiscard]] QString
	BoundTexture(size_t slot) const;

	/** The uploaded texture bound into slot `slot`; null while nothing is wired, and null when
	 *  there was no device to upload through -- the surface then samples its default. */
	[[nodiscard]] bgl::TextureAssetHandle
	BoundTextureAsset(size_t slot) const;

	[[nodiscard]] assetlib::AlphaMode
	GetAlphaMode() const noexcept
	{
		return m_AlphaMode;
	}

	[[nodiscard]] float
	GetAlphaCutoff() const noexcept
	{
		return m_AlphaCutoff;
	}

	[[nodiscard]] bool
	GetDoubleSided() const noexcept
	{
		return m_DoubleSided;
	}

	// The layer keys are authored in the properties panel (ADR-9); these are what its widgets
	// write. Each emits Changed, so the preview follows a panel edit as it follows a board edit.
	void
	SetAlphaMode(assetlib::AlphaMode mode);

	void
	SetAlphaCutoff(float cutoff);

	void
	SetDoubleSided(bool doubleSided);

private:
	void
	SyncWidgets();

	void
	PickColor(size_t index);

	void
	RefreshSwatch(size_t index);

	bgl::SurfaceType m_Surface;

	// One vec4 per declared value, seeded from the defaults; authoritative over the spin boxes,
	// which round to their decimals.
	std::vector<glm::vec4> m_Values;

	// One entry per texture slot; null while nothing is wired.
	std::vector<std::shared_ptr<SurfaceTextureData>> m_Bound;

	// One entry per texture slot and component; only a data slot's are reachable, null while
	// nothing is wired there.
	std::vector<std::array<std::shared_ptr<ChannelData>, assetlib::c_SurfaceSlotChannelCount>>
		m_Routes;

	assetlib::AlphaMode m_AlphaMode   = assetlib::AlphaMode::kOpaque;
	float               m_AlphaCutoff = 0.5f;
	bool                m_DoubleSided = true;

	QWidget*                                  m_Widget = nullptr;
	std::vector<std::vector<QDoubleSpinBox*>> m_Spins;

	// Parallel to m_Values; null everywhere but a `[Color]` value's row.
	std::vector<QPushButton*> m_Swatches;
};
