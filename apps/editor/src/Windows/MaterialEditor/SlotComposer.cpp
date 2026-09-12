#include "Windows/MaterialEditor/SlotComposer.h"

#include <QRunnable>

#include <assetlib/AssetStore.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/ImageData.h>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <memory>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <string>
#include <utility>

namespace
{
	class ComposeTask : public QRunnable
	{
	public:
		ComposeTask(
			SlotComposer*         composer,
			int                   graphIndex,
			size_t                slot,
			QString               key,
			assetlib::BMaterial   material,
			std::string           slotName,
			std::filesystem::path dataRoot) :
			m_Composer(composer), m_GraphIndex(graphIndex), m_Slot(slot), m_Key(std::move(key)),
			m_Material(std::move(material)), m_SlotName(std::move(slotName)),
			m_DataRoot(std::move(dataRoot))
		{
			setAutoDelete(true);
		}

		void
		run() override
		{
			auto image = std::shared_ptr<assetlib::ImageData>();
			try
			{
				// The compositor the bake uses -- one rule for what a routed slot's texels are.
				image = std::make_shared<assetlib::ImageData>(
					assetlib::AssetStore(m_DataRoot).ComposeSurfaceSlot(m_Material, m_SlotName));
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialEditor: could not composite a routed slot: %s", e.what());
			}

			QMetaObject::invokeMethod(
				m_Composer,
				[composer   = m_Composer,
			     graphIndex = m_GraphIndex,
			     slot       = m_Slot,
			     key        = m_Key,
			     image = std::move(image)]() { composer->Deliver(graphIndex, slot, key, image); },
				Qt::QueuedConnection);
		}

	private:
		SlotComposer*         m_Composer   = nullptr;
		int                   m_GraphIndex = 0;
		size_t                m_Slot       = 0;
		QString               m_Key;
		assetlib::BMaterial   m_Material;
		std::string           m_SlotName;
		std::filesystem::path m_DataRoot;
	};
}

SlotComposer::SlotComposer(QObject* parent) : QObject(parent)
{
	// A compose holds every decoded source of one slot at once -- tens of MB each at 2K.
	m_Pool.setMaxThreadCount(1);
}

SlotComposer::~SlotComposer()
{
	// Queued composes are dropped, not waited for: ~QThreadPool blocks on the whole queue, and a
	// burst of rewires would otherwise stall the close by seconds. The one running compose still
	// finishes; its delivery posts to a dying object and is discarded.
	m_Pool.clear();
}

void
SlotComposer::Compose(
	int                   graphIndex,
	size_t                slot,
	QString               key,
	assetlib::BMaterial   material,
	std::string           slotName,
	std::filesystem::path dataRoot)
{
	m_Pool.start(new ComposeTask(
		this,
		graphIndex,
		slot,
		std::move(key),
		std::move(material),
		std::move(slotName),
		std::move(dataRoot)));
}

void
SlotComposer::Deliver(
	int                                  graphIndex,
	size_t                               slot,
	const QString&                       key,
	std::shared_ptr<assetlib::ImageData> image)
{
	Q_EMIT Composed(graphIndex, slot, key, std::move(image));
}
