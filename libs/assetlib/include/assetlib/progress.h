#pragma once

namespace assetlib
{
	/** What a cook is doing when it reports. One per kind of work a long operation splits into. */
	enum class ProgressPhase
	{
		kScanning,            // reading headers and documents to decide what has to be done
		kRegenerating,        // producing a `.bmesh`, `.bskel` or `.banim` from its source
		kExtractingTextures,  // writing a source's detached images as `.ktx2`
		kBakingMaterials,     // re-baking a `.bmaterial`'s triplet from the maps it routes
		kResaving,            // rewriting a container at the form the current serializer writes
	};

	/** The step a cook is reporting: what it is doing, to what, and how far along the run is. */
	struct ProgressEvent
	{
		ProgressPhase phase = ProgressPhase::kScanning;

		// The mount key being worked on, or empty where the step is not about one file.
		std::string_view subject;

		// Steps finished before this one, out of the steps expected. Both belong to whichever
		// operation is reporting: one that runs another inside itself lets the inner one report in
		// its own frame, so `total` changes as a run moves between them and a reader must take the
		// one each event carries. A zero `total` means it cannot say, and reports only the phase
		// and the subject.
		size_t done  = 0;
		size_t total = 0;
	};

	/**
	 * Where a long cook reports what it is doing. The one progress seam in this library: every
	 * operation that can take longer than a frame takes one of these and nothing else.
	 *
	 * **Calls are serialized and may arrive from any thread.** A cook that runs its work across
	 * threads holds a lock across the call, so a sink needs no lock of its own -- but it must not
	 * assume the calling thread, and it must not block, because every worker waits behind it.
	 *
	 * An empty sink is the default everywhere and costs a branch per step.
	 */
	using ProgressSink = std::function<void(const ProgressEvent&)>;

	/**
	 * `sink` behind a lock of its own, for a cook that reports from several threads. The one place
	 * the serialization rule above is kept, so no sink and no threaded cook has to keep it twice.
	 *
	 * An empty sink stays empty: there is nothing to serialize, and a lock per step is not free.
	 */
	inline ProgressSink
	serialized(ProgressSink sink)
	{
		if (!sink)
			return {};

		auto guard = std::make_shared<std::mutex>();
		return [sink = std::move(sink), guard](const ProgressEvent& event) {
			const auto held = std::lock_guard(*guard);
			sink(event);
		};
	}

	/** Reports one step to `sink` if there is one. */
	inline void
	reportStep(
		const ProgressSink& sink,
		ProgressPhase       phase,
		std::string_view    subject,
		size_t              done  = 0,
		size_t              total = 0)
	{
		if (sink)
			sink(ProgressEvent{ phase, subject, done, total });
	}
}
