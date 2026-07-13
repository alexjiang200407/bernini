#include "Async/BackgroundTask.h"

#include "util/Modal.h"
#include "util/QtSupport.h"

#include <QCloseEvent>
#include <QPushButton>
#include <QThread>
#include <QtTest>
#include <assetlib/cancel.h>

namespace
{
	using editor::test::c_WaitMs;
	using editor::test::CancelLoadingScreen;
	using editor::test::OnLoadingScreen;

	/**
	 * Blocks the *worker* thread until `ready` is set, and reports whether it was.
	 *
	 * Deliberately not editor::test::WaitFor, which pumps the event loop: this runs off the UI thread,
	 * where doing that would be a bug. And a worker that waits must have a deadline -- the loading
	 * screen refuses to close while the worker is running, so a worker that waits forever hangs the
	 * whole suite rather than failing one test.
	 */
	bool
	WaitOnWorker(const std::atomic<bool>& ready)
	{
		QDeadlineTimer deadline(c_WaitMs);
		while (!ready.load() && !deadline.hasExpired()) QThread::msleep(1);

		return ready.load();
	}

	/** Blocks the worker until cancellation is asked for. */
	void
	WaitForCancellation(const background::Progress& progress)
	{
		QDeadlineTimer deadline(c_WaitMs);
		while (!progress.Cancellation().stop_requested() && !deadline.hasExpired())
			QThread::msleep(1);
	}
}

TEST_CASE("A task result knows its own status", "[background]")
{
	const background::TaskResult completed = { background::TaskStatus::kCompleted, {} };
	const background::TaskResult cancelled = { background::TaskStatus::kCancelled, {} };
	const background::TaskResult failed    = { background::TaskStatus::kFailed, "boom" };

	REQUIRE((completed.Completed() && !completed.Cancelled() && !completed.Failed()));
	REQUIRE((cancelled.Cancelled() && !cancelled.Completed() && !cancelled.Failed()));
	REQUIRE((failed.Failed() && !failed.Completed() && !failed.Cancelled()));
}

TEST_CASE("Work that returns completes", "[background]")
{
	bool ran = false;

	const background::TaskResult result =
		background::RunWithLoadingScreen(nullptr, "Working", [&](background::Progress&) {
			ran = true;
		});

	REQUIRE(ran);
	REQUIRE(result.Completed());
	REQUIRE(result.error.isEmpty());
}

TEST_CASE("Work that throws fails with the exception's message", "[background]")
{
	// The message is the whole point: it is what the editor puts in front of the user, so an
	// exception must not be flattened into a generic "import failed".
	const background::TaskResult result =
		background::RunWithLoadingScreen(nullptr, "Working", [](background::Progress&) {
			throw std::runtime_error("cannot overwrite 'a.bmesh'");
		});

	REQUIRE(result.Failed());
	REQUIRE(result.error == QString("cannot overwrite 'a.bmesh'"));
}

TEST_CASE("Work that throws something unknown still fails", "[background]")
{
	// A throw that escapes QRunnable::run() takes the process down, so even a type nothing here knows
	// about has to come back as a failure.
	const background::TaskResult result =
		background::RunWithLoadingScreen(nullptr, "Working", [](background::Progress&) {
			throw 42;
		});

	REQUIRE(result.Failed());
	REQUIRE(result.error == QString("an unknown error occurred"));
}

TEST_CASE("Cancelling is an outcome rather than a failure", "[background]")
{
	// assetlib::Cancelled deliberately does not derive from std::runtime_error, and this is why: a
	// caller that reports every failure to the user must not report a cancel as one.
	const background::TaskResult result = background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[](background::Progress&) { throw assetlib::Cancelled(); },
		background::Cancellable::kYes);

	REQUIRE(result.Cancelled());
	REQUIRE(!result.Failed());
	REQUIRE(result.error.isEmpty());
}

TEST_CASE("Cancellation starts unrequested", "[background]")
{
	bool requestedAtStart = true;

	background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[&](background::Progress& progress) {
			requestedAtStart = progress.Cancellation().stop_requested();
		},
		background::Cancellable::kYes);

	REQUIRE(!requestedAtStart);
}

TEST_CASE("Pressing cancel requests cancellation", "[background]")
{
	CancelLoadingScreen();

	bool sawTheRequest = false;

	const background::TaskResult result = background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[&](background::Progress& progress) {
			WaitForCancellation(progress);
			sawTheRequest = progress.Cancellation().stop_requested();

			// Cooperative: the token going hot is a request, and the worker is what turns it into an
			// outcome.
			assetlib::throwIfCancelled(progress.Cancellation());
		},
		background::Cancellable::kYes);

	REQUIRE(sawTheRequest);
	REQUIRE(result.Cancelled());
}

TEST_CASE("A cancel button is offered only when the work can be cancelled", "[background]")
{
	const background::Cancellable cancellable =
		GENERATE(background::Cancellable::kNo, background::Cancellable::kYes);
	const bool expected = cancellable == background::Cancellable::kYes;

	INFO("cancellable: " << expected);

	std::atomic<bool> looked = false;
	bool              found  = false;

	OnLoadingScreen([&](QProgressDialog& dialog) {
		found = dialog.findChild<QPushButton*>() != nullptr;
		looked.store(true);
		return true;
	});

	// Work that cannot be unwound safely must not offer a button that says it can.
	background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[&](background::Progress&) { WaitOnWorker(looked); },
		cancellable);

	REQUIRE(looked.load());
	REQUIRE(found == expected);
}

TEST_CASE("Progress reaches the loading screen", "[background]")
{
	std::atomic<bool> shown = false;

	QString label;
	int     value   = -1;
	int     maximum = -1;

	OnLoadingScreen([&](QProgressDialog& dialog) {
		// Report is queued to the UI thread, so the first poll after it may still predate it.
		// Keep looking until the report lands.
		if (dialog.labelText() != "Extracting textures")
			return false;

		label   = dialog.labelText();
		value   = dialog.value();
		maximum = dialog.maximum();
		shown.store(true);
		return true;
	});

	background::RunWithLoadingScreen(nullptr, "Working", [&](background::Progress& progress) {
		progress.Report(3, 10, "Extracting textures");
		WaitOnWorker(shown);
	});

	REQUIRE(shown.load());
	REQUIRE(label == QString("Extracting textures"));
	REQUIRE(value == 3);
	REQUIRE(maximum == 10);
}

TEST_CASE("A cancelled worker's progress does not overwrite 'Cancelling...'", "[background]")
{
	std::atomic<bool> cancelPressed = false;
	std::atomic<bool> lateReported  = false;

	bool stillUp          = true;
	bool cancellingShown  = false;
	bool cancellingHeld   = true;
	bool cancelStayedLive = false;

	OnLoadingScreen([&](QProgressDialog& dialog) {
		if (!cancelPressed.load())
		{
			auto* cancel = dialog.findChild<QPushButton*>();
			if (cancel == nullptr)
				return false;

			QTest::mouseClick(cancel, Qt::LeftButton);
			cancelPressed.store(true);
			return false;
		}

		if (!dialog.isVisible())
			stillUp = false;

		if (dialog.labelText() == "Cancelling...")
			cancellingShown = true;

		// The worker's reports describe work that is about to be thrown away. Letting one through
		// would replace "Cancelling..." with a label and a bar that both claim the import is still
		// going somewhere.
		if (cancellingShown && dialog.labelText() != "Cancelling...")
			cancellingHeld = false;

		// And there is nothing left to cancel a second time.
		if (auto* cancel = dialog.findChild<QPushButton*>();
		    cancel != nullptr && cancel->isEnabled())
			cancelStayedLive = true;

		// Keep watching until the worker's late report has had time to arrive.
		return lateReported.load();
	});

	const background::TaskResult result = background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[&](background::Progress& progress) {
			WaitOnWorker(cancelPressed);

			progress.Report(9, 10, "Still working");
			QThread::msleep(50);  // let the queued report reach the UI thread, if it is going to

			lateReported.store(true);
			QThread::msleep(50);  // and let the poll above see the state it left behind

			assetlib::throwIfCancelled(progress.Cancellation());
		},
		background::Cancellable::kYes);

	REQUIRE(stillUp);
	REQUIRE(cancellingShown);
	REQUIRE(cancellingHeld);
	REQUIRE(!cancelStayedLive);
	REQUIRE(result.Cancelled());
}

TEST_CASE("Closing the loading screen cancels rather than dismissing it", "[background]")
{
	std::atomic<bool> closed = false;

	bool stayedUp = false;

	OnLoadingScreen([&](QProgressDialog& dialog) {
		QCloseEvent close;
		QCoreApplication::sendEvent(&dialog, &close);

		// The X button asks the worker to stop; it does not get to walk away from work that is
		// still running and leave the editor live behind it.
		stayedUp = dialog.isVisible() && !close.isAccepted();
		closed.store(true);
		return true;
	});

	const background::TaskResult result = background::RunWithLoadingScreen(
		nullptr,
		"Working",
		[&](background::Progress& progress) {
			WaitOnWorker(closed);
			WaitForCancellation(progress);

			assetlib::throwIfCancelled(progress.Cancellation());
		},
		background::Cancellable::kYes);

	REQUIRE(stayedUp);
	REQUIRE(result.Cancelled());
}
