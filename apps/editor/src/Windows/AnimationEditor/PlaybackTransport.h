#pragma once

namespace editor
{
	/**
	 * One playable clip, in source-asset terms. Its own type rather than gamelib's
	 * `ClipInfo` so the transport names no backend: the panel fills it from a VAT
	 * acquire today and a skinned source can fill it tomorrow without a VAT-named struct in the
	 * signature.
	 */
	struct ClipInfo
	{
		std::string name;
		uint32_t    frameCount = 0;
		float       sampleRate = 30.0f;
		float       duration   = 0.0f;
		bool        loop       = false;
	};

	/**
	 * The clip transport behind the Animation panel: pure time arithmetic over a clip table, no
	 * Qt and no bgl. The preview instance is always {clip, phase 0, rate 1}, so this clock *is*
	 * the whole transport: GetTimeSeconds is what the panel writes into RenderJob::time, and
	 * GetCurrentFrame mirrors the shader's ClipFrames exactly -- a looping clip wraps at `frameCount`
	 * frames, a one-shot clamps to its last. Frame math derives from frameCount and sampleRate;
	 * the recorded duration is metadata for display, never arithmetic.
	 */
	class PlaybackTransport
	{
	public:
		/**
		 * Replaces the clip table: clip 0 selected, time zero, paused. An empty table is inert.
		 * @throws std::runtime_error on a clip with no frames or a non-positive sample rate.
		 */
		void
		SetClips(std::vector<ClipInfo> clips);

		/** Rewinds onto `index`; play state and speed carry over. @throws std::runtime_error if `index` is outside the table. */
		void
		SelectClip(uint32_t index);

		/** Starts advancing; a one-shot parked on its end rewinds first. */
		void
		Play() noexcept;

		void
		Pause() noexcept;

		/** Multiplies wall time in Advance. Negative plays backwards: a loop wraps, a one-shot parks at zero. */
		void
		SetSpeed(float speed) noexcept;

		/** Moves the clock by `dtSeconds` of wall time while playing; paused (or clipless) it holds. */
		void
		Advance(float dtSeconds) noexcept;

		/** Parks the clock at `seconds`, wrapped or clamped into the clip's domain. Play state is untouched. */
		void
		Scrub(float seconds) noexcept;

		/** Pauses, then moves `frames` whole frames from the nearest frame, wrapped or clamped. */
		void
		StepFrames(int frames) noexcept;

		[[nodiscard]] bool
		IsPlaying() const noexcept;

		[[nodiscard]] float
		GetSpeed() const noexcept;

		/** The clip-local clock in seconds -- what RenderJob::time is fed. Zero with no clips. */
		[[nodiscard]] float
		GetTimeSeconds() const noexcept;

		/** The fractional frame the shader samples at GetTimeSeconds. */
		[[nodiscard]] float
		GetCurrentFrame() const noexcept;

		/**
		 * One period of the active clip in seconds -- where the timeline ends: the wrap point
		 * `frameCount / sampleRate` for a loop, the last frame `(frameCount - 1) / sampleRate`
		 * for a one-shot. Zero with no clips.
		 */
		[[nodiscard]] float
		GetPeriodSeconds() const noexcept;

		[[nodiscard]] bool
		HasClips() const noexcept;

		[[nodiscard]] const std::vector<ClipInfo>&
		GetClips() const noexcept;

		[[nodiscard]] uint32_t
		GetActiveClipIndex() const noexcept;

		/** @throws std::runtime_error with an empty table. */
		[[nodiscard]] const ClipInfo&
		GetActiveClip() const;

	private:
		[[nodiscard]] float
		Normalized(float seconds) const noexcept;

		std::vector<ClipInfo> m_Clips;

		uint32_t m_ActiveClip = 0;
		float    m_Time       = 0.0f;
		float    m_Speed      = 1.0f;
		bool     m_Playing    = false;
	};
}
