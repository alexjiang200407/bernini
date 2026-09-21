#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace editor
{
	/**
	 * One playable clip, in source-asset terms. Its own type rather than gamelib's `ClipInfo`, so
	 * the transport names no backend: what fills it is the acquire, and the transport is the same
	 * whichever pose source the instances on it draw through.
	 */
	struct ClipInfo
	{
		std::string name;
		uint32_t    frameCount = 0;
		float       sampleRate = 30.0f;
		float       duration   = 0.0f;
		bool        loop       = false;

		// Units per second over the ground, measured at cook; zero for a clip that does not travel.
		// What a locomotion space's thresholds are taken from.
		float locomotionSpeed = 0.0f;
	};

	/**
	 * The clip transport behind the Animation panel: pure time arithmetic over a clip table, no
	 * Qt and no bgl. The preview instance is always {clip, phase 0, rate 1}, so this clock *is*
	 * the whole transport: GetTimeSeconds is what the panel writes into RenderJob::time, and
	 * GetCurrentFrame mirrors the shader's ClipFrames exactly -- both span `frameCount - 1` frame
	 * intervals, a looping clip wrapping over them and a one-shot clamping to the last. That span is
	 * the whole clip either way: frames cover the closed interval [0, duration], and a loop's last
	 * frame repeats its first. Frame math derives from frameCount and sampleRate; the recorded
	 * duration is metadata for display, never arithmetic.
	 *
	 * Mirroring the shader is a requirement, not a convenience: the panel's playhead and the pose on
	 * screen come from these two separate pieces of arithmetic, so a difference between them shows
	 * up as a scrubber that lies.
	 *
	 * The clock has a second domain, and which one it is in is `InTransitionWindow`. A crossfade's
	 * ramp is stamped in absolute time, so a clip-local clock that wraps would re-read the ramp's
	 * start on every loop -- the hazard docs/skinning.md records as the reason the panel's foot-IK
	 * sliders are constants and never fades. A transition window is therefore absolute, clamped at
	 * both ends and never wrapped, whatever the active clip does.
	 */
	class PlaybackTransport
	{
	public:
		/**
		 * Replaces the clip table: clip 0 selected, time zero, paused, any transition window
		 * cleared. An empty table is inert.
		 * @throws std::runtime_error on a clip with no frames or a non-positive sample rate.
		 */
		void
		SetClips(std::vector<ClipInfo> clips);

		/** Rewinds onto `index` and clears any transition window; play state and speed carry over. @throws std::runtime_error if `index` is outside the table. */
		void
		SelectClip(uint32_t index);

		/**
		 * Puts the clock on `[startSeconds, endSeconds]` of absolute time and parks it at the
		 * start. Play state and speed carry over, as `SelectClip`'s rewind does.
		 *
		 * The window is the caller's to bracket: a crossfade stamped at `t0` over `duration` wants
		 * lead-in before it and tail after, so what has already been decided can be seen going in
		 * and what settles can be seen coming out.
		 *
		 * @throws std::runtime_error if the window is empty or reversed, or either end is not finite.
		 */
		void
		SetTransitionWindow(float startSeconds, float endSeconds);

		/** Returns the clock to the active clip's own domain, rewound. Harmless with no window. */
		void
		ClearTransitionWindow() noexcept;

		[[nodiscard]] bool
		InTransitionWindow() const noexcept;

		[[nodiscard]] float
		GetWindowStartSeconds() const noexcept;

		[[nodiscard]] float
		GetWindowEndSeconds() const noexcept;

		/** Starts advancing; a one-shot or a transition window parked on its end rewinds first. */
		void
		Play() noexcept;

		void
		Pause() noexcept;

		/** Multiplies wall time in Advance. Negative plays backwards: a loop wraps, a one-shot parks at zero, a transition window at its start. */
		void
		SetSpeed(float speed) noexcept;

		/** Moves the clock by `dtSeconds` of wall time while playing; paused (or clipless) it holds. */
		void
		Advance(float dtSeconds) noexcept;

		/** Parks the clock at `seconds`, wrapped or clamped into the current domain. Play state is untouched. */
		void
		Scrub(float seconds) noexcept;

		/**
		 * Pauses, then moves `frames` whole frames from the nearest frame, wrapped or clamped. In a
		 * transition window a frame is the active clip's sample interval, since the window's own
		 * domain is seconds and has no frames of its own.
		 */
		void
		StepFrames(int frames) noexcept;

		[[nodiscard]] bool
		IsPlaying() const noexcept;

		[[nodiscard]] float
		GetSpeed() const noexcept;

		/**
		 * The clock in seconds -- what RenderJob::time is fed. Clip-local, or absolute inside a
		 * transition window. Zero with no clips.
		 */
		[[nodiscard]] float
		GetTimeSeconds() const noexcept;

		/**
		 * The fractional frame the shader samples at GetTimeSeconds, or nothing inside a transition
		 * window: two slots are live over a fade and neither one's frame is the playhead. Empty
		 * rather than zero because zero is a frame -- the first one -- so a caller that forgot the
		 * window would read a plausible number instead of no number.
		 */
		[[nodiscard]] std::optional<float>
		GetCurrentFrame() const noexcept;

		/**
		 * One period of the active clip in seconds -- for a loop where it wraps:
		 * `(frameCount - 1) / sampleRate`, the clip's `frameCount` frames being the ends of that
		 * many intervals. Zero with no clips. This is the clip's own span whatever domain the clock
		 * is in; a transition window's ends are its own.
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

		uint32_t m_ActiveClip  = 0;
		float    m_Time        = 0.0f;
		float    m_Speed       = 1.0f;
		bool     m_Playing     = false;
		bool     m_InWindow    = false;
		float    m_WindowStart = 0.0f;
		float    m_WindowEnd   = 0.0f;
	};
}
