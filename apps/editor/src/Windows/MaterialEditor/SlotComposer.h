#pragma once

#include <QThreadPool>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include <assetlib_structs/BMaterial.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace assetlib
{
	struct ImageData;
}

/**
 * Composites routed surface slots off the UI thread (ADR-8's editor half): the compositor is the
 * bake's own and decodes every source at full resolution, which is far too slow for a rewire to
 * wait on -- close to a second for a 2K slot in a debug build.
 *
 * One compose runs at a time, bounding the decode memory and landing deliveries in request order.
 * Each finishes with Composed on the UI thread, echoing the caller's addressing verbatim, so a
 * delivery can be matched against a board that may have moved on since the request.
 */
class SlotComposer : public QObject
{
	Q_OBJECT

public:
	explicit SlotComposer(QObject* parent = nullptr);

	~SlotComposer() override;

	/** Queues one compose of `material`'s slot `slotName` against `dataRoot`. `graphIndex`,
	 *  `slot` and `key` are only echoed back on Composed; nothing here reads them. */
	void
	Compose(
		int                   graphIndex,
		size_t                slot,
		QString               key,
		assetlib::BMaterial   material,
		std::string           slotName,
		std::filesystem::path dataRoot);

	/** Hands a finished compose back. Called by a worker via a queued invocation, so it always
	 *  runs on the UI thread. A null `image` means the compose failed. */
	void
	Deliver(
		int                                  graphIndex,
		size_t                               slot,
		const QString&                       key,
		std::shared_ptr<assetlib::ImageData> image);

signals:
	void
	Composed(
		int                                  graphIndex,
		size_t                               slot,
		const QString&                       key,
		std::shared_ptr<assetlib::ImageData> image);

private:
	QThreadPool m_Pool;
};
