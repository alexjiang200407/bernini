#include "LodBar.h"

#include "Windows/MeshEditor/lod_view.h"

#include <editor_plugin_api/ILanguageResolver.h>
#include <editor_plugin_api/localize.h>

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QSignalBlocker>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <cmath>
#include <cstdint>
#include <optional>
#include <qnamespace.h>
#include <qobject.h>
#include <qsizepolicy.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>

namespace
{
	enum Column : int
	{
		kLevelColumn,
		kTrianglesColumn,
		kDrawnFromColumn,
		kColumnCount,
	};

	// Past this a size reads as the camera standing inside the mesh's bound.
	constexpr float c_InsidePixels = 1.0e30f;

	QString
	PixelsText(const editor::ILanguageResolver& language, float pixels)
	{
		if (pixels >= c_InsidePixels)
			return editor::Localize(
				language,
				"bernini.material.lod_pixels_inside",
				"fills the view");
		return editor::Localize(
			language,
			"bernini.material.lod_pixels",
			{ static_cast<int64_t>(std::lround(pixels)) },
			"{0} px on screen");
	}
}

LodBar::LodBar(const editor::ILanguageResolver& language, QWidget* parent) :
	QWidget(parent), m_Language(language)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);

	auto* header = new QHBoxLayout();
	header->setContentsMargins(0, 0, 0, 0);
	header->addWidget(new QLabel(
		editor::Localize(language, "bernini.material.lod_label", "Level of Detail"),
		this));

	m_Selector = new QComboBox(this);
	m_Selector->setObjectName(QStringLiteral("LodSelector"));
	m_Selector->setToolTip(
		editor::Localize(
			language,
			"bernini.material.lod_selector_tooltip",
			"Auto draws the level the game would at this size on screen. A level pins it, however "
			"near or far the camera is."));
	header->addWidget(m_Selector);

	m_Readout = new QLabel(this);
	m_Readout->setObjectName(QStringLiteral("LodReadout"));
	m_Readout->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	header->addWidget(m_Readout, 1);
	layout->addLayout(header);

	m_Table = new QTableWidget(0, kColumnCount, this);
	m_Table->setObjectName(QStringLiteral("LodTable"));
	m_Table->setHorizontalHeaderLabels(
		{ editor::Localize(language, "bernini.material.lod_level_column", "Level"),
	      editor::Localize(language, "bernini.material.lod_triangles_column", "Triangles"),
	      editor::Localize(language, "bernini.material.lod_drawn_from_column", "Drawn From") });
	m_Table->verticalHeader()->hide();
	m_Table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_Table->setSelectionMode(QAbstractItemView::NoSelection);
	m_Table->setFocusPolicy(Qt::NoFocus);
	layout->addWidget(m_Table);

	connect(m_Selector, &QComboBox::currentIndexChanged, this, [this]() {
		Q_EMIT ForcedLodChosen(GetForcedLod());
	});

	hide();
}

std::optional<uint32_t>
LodBar::GetForcedLod() const
{
	const QVariant level = m_Selector->currentData();
	if (!level.isValid())
		return std::nullopt;
	return level.toUInt();
}

void
LodBar::ShowLods(const editor::MeshLods* lods, std::optional<uint32_t> forced)
{
	if (lods == nullptr || lods->minPixels.empty())
	{
		hide();
		return;
	}

	const auto count = static_cast<uint32_t>(lods->minPixels.size());
	{
		const QSignalBlocker blocker(m_Selector);
		m_Selector->clear();
		m_Selector->addItem(editor::Localize(m_Language, "bernini.material.lod_auto", "Auto"));
		for (uint32_t level = 0; level < count; ++level)
		{
			m_Selector->addItem(
				editor::Localize(m_Language, "bernini.material.lod_level", { level }, "LOD {0}"),
				level);
		}
		m_Selector->setCurrentIndex(
			forced.has_value() && *forced < count ? static_cast<int>(*forced) + 1 : 0);
	}

	m_Table->setRowCount(static_cast<int>(count));
	const QLocale locale;
	for (uint32_t level = 0; level < count; ++level)
	{
		const float minPixels = lods->minPixels[level];
		const auto  row       = static_cast<int>(level);

		const QString drawnFrom =
			minPixels > 0.0f ?
				editor::Localize(
					m_Language,
					"bernini.material.lod_drawn_from",
					{ static_cast<int64_t>(std::lround(minPixels)) },
					"{0} px") :
				editor::Localize(m_Language, "bernini.material.lod_drawn_always", "any size");

		m_Table->setItem(
			row,
			kLevelColumn,
			new QTableWidgetItem(
				editor::Localize(m_Language, "bernini.material.lod_level", { level }, "LOD {0}")));
		m_Table->setItem(
			row,
			kTrianglesColumn,
			new QTableWidgetItem(locale.toString(lods->triangles[level])));
		m_Table->setItem(row, kDrawnFromColumn, new QTableWidgetItem(drawnFrom));
	}

	const int rows = m_Table->horizontalHeader()->height() + m_Table->verticalHeader()->length() +
	                 2 * m_Table->frameWidth();
	m_Table->setFixedHeight(rows);

	show();
}

void
LodBar::ShowReadout(std::optional<editor::LodReadout> readout, bool forced)
{
	const int rows = m_Table->rowCount();
	for (int row = 0; row < rows; ++row)
	{
		const bool drawn = readout.has_value() && static_cast<int>(readout->level) == row;
		for (int column = 0; column < kColumnCount; ++column)
		{
			QTableWidgetItem* item = m_Table->item(row, column);
			if (item == nullptr)
				continue;
			QFont font = item->font();
			font.setBold(drawn);
			item->setFont(font);
		}
	}

	if (!readout.has_value())
	{
		m_Readout->clear();
		return;
	}

	const QString size = PixelsText(m_Language, readout->pixels);
	if (static_cast<int>(readout->level) >= rows)
	{
		m_Readout->setText(
			editor::Localize(
				m_Language,
				"bernini.material.lod_readout_nothing",
				{ size },
				"Nothing drawn: {0}, under every level"));
	}
	else if (forced)
	{
		m_Readout->setText(
			editor::Localize(
				m_Language,
				"bernini.material.lod_readout_pinned",
				{ readout->level, size },
				"Pinned to LOD {0}: {1}"));
	}
	else
	{
		m_Readout->setText(
			editor::Localize(
				m_Language,
				"bernini.material.lod_readout_auto",
				{ readout->level, size },
				"Drawing LOD {0}: {1}"));
	}
}
