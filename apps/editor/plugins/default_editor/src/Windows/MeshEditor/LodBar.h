#pragma once

#include "Windows/MeshEditor/lod_view.h"

#include <editor_plugin_api/ILanguageResolver.h>

#include <QWidget>

#include <cstdint>
#include <optional>
#include <qtmetamacros.h>

class QComboBox;
class QLabel;
class QTableWidget;

/**
 * The strip under the Mesh Editor's preview: the shown mesh's levels of detail, their triangles and
 * the size each is drawn from, which one is drawn now, and a pin to hold one. A widget of its own
 * rather than an overlay, because the preview is a native surface nothing composites over.
 */
class LodBar : public QWidget
{
	Q_OBJECT

public:
	/** `language` must outlive the bar. */
	LodBar(const editor::ILanguageResolver& language, QWidget* parent);

	/** Lists `lods` with `forced` pinned, or hides the bar for null -- nothing a level describes. */
	void
	ShowLods(const editor::MeshLods* lods, std::optional<uint32_t> forced);

	/** Marks the drawn level and says what chose it; nullopt, before the view has a size, blanks it. */
	void
	ShowReadout(std::optional<editor::LodReadout> readout, bool forced);

	/** The level the selector pins, or nullopt for Auto. */
	[[nodiscard]] std::optional<uint32_t>
	GetForcedLod() const;

Q_SIGNALS:
	void
	ForcedLodChosen(std::optional<uint32_t> level);

private:
	const editor::ILanguageResolver& m_Language;
	QComboBox*                       m_Selector = nullptr;
	QTableWidget*                    m_Table    = nullptr;
	QLabel*                          m_Readout  = nullptr;
};
