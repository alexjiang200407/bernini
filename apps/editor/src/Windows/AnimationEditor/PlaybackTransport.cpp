#include "PlaybackTransport.h"

#include <algorithm>
#include <cmath>
#include <core/err/util.h>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace editor
{
	void
	PlaybackTransport::SetClips(std::vector<ClipInfo> clips)
	{
		for (const ClipInfo& clip : clips)
			core::throw_runtime_error_if(
				clip.frameCount == 0 || clip.sampleRate <= 0.0f,
				"PlaybackTransport: clip '{}' has {} frames at {} Hz",
				clip.name,
				clip.frameCount,
				clip.sampleRate);

		m_Clips      = std::move(clips);
		m_ActiveClip = 0;
		m_Time       = 0.0f;
		m_Playing    = false;
		m_InWindow   = false;
	}

	void
	PlaybackTransport::SelectClip(const uint32_t index)
	{
		core::throw_runtime_error_if(
			index >= m_Clips.size(),
			"PlaybackTransport: clip {} out of range ({} clips)",
			index,
			m_Clips.size());

		m_ActiveClip = index;
		m_Time       = 0.0f;
		m_InWindow   = false;
	}

	void
	PlaybackTransport::SetTransitionWindow(const float startSeconds, const float endSeconds)
	{
		core::throw_runtime_error_if(
			!std::isfinite(startSeconds) || !std::isfinite(endSeconds) ||
				endSeconds <= startSeconds,
			"PlaybackTransport: transition window [{}, {}] is empty or reversed",
			startSeconds,
			endSeconds);

		m_InWindow    = true;
		m_WindowStart = startSeconds;
		m_WindowEnd   = endSeconds;
		m_Time        = startSeconds;
	}

	void
	PlaybackTransport::ClearTransitionWindow() noexcept
	{
		if (!m_InWindow)
			return;

		m_InWindow = false;
		m_Time     = 0.0f;
	}

	bool
	PlaybackTransport::InTransitionWindow() const noexcept
	{
		return m_InWindow;
	}

	float
	PlaybackTransport::GetWindowStartSeconds() const noexcept
	{
		return m_WindowStart;
	}

	float
	PlaybackTransport::GetWindowEndSeconds() const noexcept
	{
		return m_WindowEnd;
	}

	void
	PlaybackTransport::Play() noexcept
	{
		if (!HasClips())
			return;

		if (m_InWindow)
		{
			if (m_Time >= m_WindowEnd)
				m_Time = m_WindowStart;
		}
		else if (
			const auto& clip = m_Clips[m_ActiveClip]; !clip.loop && m_Time >= GetPeriodSeconds())
		{
			m_Time = 0.0f;
		}

		m_Playing = true;
	}

	void
	PlaybackTransport::Pause() noexcept
	{
		m_Playing = false;
	}

	void
	PlaybackTransport::SetSpeed(const float speed) noexcept
	{
		m_Speed = speed;
	}

	void
	PlaybackTransport::Advance(const float dtSeconds) noexcept
	{
		if (!m_Playing || !HasClips())
			return;

		m_Time = Normalized(m_Time + dtSeconds * m_Speed);
	}

	void
	PlaybackTransport::Scrub(const float seconds) noexcept
	{
		if (!HasClips())
			return;

		m_Time = Normalized(seconds);
	}

	void
	PlaybackTransport::StepFrames(const int frames) noexcept
	{
		if (!HasClips())
			return;

		m_Playing = false;

		const auto& clip = m_Clips[m_ActiveClip];

		if (m_InWindow)
		{
			const float interval = static_cast<float>(frames) / clip.sampleRate;
			m_Time               = std::clamp(m_Time + interval, m_WindowStart, m_WindowEnd);
			return;
		}

		// The span both kinds of clip cover, matching clip_playback.slang: frameCount frames are the
		// ends of frameCount - 1 intervals. Floored at 1 because a one-frame clip would otherwise
		// take a modulo by zero -- the importer never marks one looping, but nothing here checks.
		const int cycle = std::max(1, static_cast<int>(clip.frameCount) - 1);

		int frame = static_cast<int>(std::lround(GetCurrentFrame().value())) + frames;
		if (clip.loop)
			frame = ((frame % cycle) + cycle) % cycle;
		else
			frame = std::clamp(frame, 0, cycle);

		m_Time = static_cast<float>(frame) / clip.sampleRate;
	}

	bool
	PlaybackTransport::IsPlaying() const noexcept
	{
		return m_Playing;
	}

	float
	PlaybackTransport::GetSpeed() const noexcept
	{
		return m_Speed;
	}

	float
	PlaybackTransport::GetTimeSeconds() const noexcept
	{
		return m_Time;
	}

	std::optional<float>
	PlaybackTransport::GetCurrentFrame() const noexcept
	{
		if (!HasClips() || m_InWindow)
			return std::nullopt;

		// The shader's ClipFrames with phase 0 and rate 1; m_Time is already in the clip's
		// domain, so the wrap/clamp below only guards the exact period boundary.
		const auto& clip   = m_Clips[m_ActiveClip];
		const float cycle  = std::max(1.0f, static_cast<float>(clip.frameCount) - 1.0f);
		float       frames = m_Time * clip.sampleRate;

		if (clip.loop)
		{
			frames = std::fmod(frames, cycle);
			if (frames < 0.0f)
				frames += cycle;
		}
		else
		{
			frames = std::clamp(frames, 0.0f, cycle);
		}

		return frames;
	}

	float
	PlaybackTransport::GetPeriodSeconds() const noexcept
	{
		if (!HasClips())
			return 0.0f;

		const auto& clip = m_Clips[m_ActiveClip];
		return std::max(1.0f, static_cast<float>(clip.frameCount) - 1.0f) / clip.sampleRate;
	}

	float
	PlaybackTransport::GetNormalizedPosition() const noexcept
	{
		if (!HasClips())
			return 0.0f;

		if (m_InWindow)
			return (m_Time - m_WindowStart) / (m_WindowEnd - m_WindowStart);

		const float period = GetPeriodSeconds();
		return period > 0.0f ? m_Time / period : 0.0f;
	}

	void
	PlaybackTransport::ScrubNormalized(const float position) noexcept
	{
		if (!HasClips())
			return;

		const float at = std::clamp(position, 0.0f, 1.0f);
		Scrub(
			m_InWindow ? m_WindowStart + at * (m_WindowEnd - m_WindowStart) :
						 at * GetPeriodSeconds());
	}

	bool
	PlaybackTransport::HasClips() const noexcept
	{
		return !m_Clips.empty();
	}

	const std::vector<ClipInfo>&
	PlaybackTransport::GetClips() const noexcept
	{
		return m_Clips;
	}

	uint32_t
	PlaybackTransport::GetActiveClipIndex() const noexcept
	{
		return m_ActiveClip;
	}

	const ClipInfo&
	PlaybackTransport::GetActiveClip() const
	{
		core::throw_runtime_error_if(!HasClips(), "PlaybackTransport: no clips loaded");
		return m_Clips[m_ActiveClip];
	}

	float
	PlaybackTransport::Normalized(const float seconds) const noexcept
	{
		if (m_InWindow)
			return std::clamp(seconds, m_WindowStart, m_WindowEnd);

		const auto& clip   = m_Clips[m_ActiveClip];
		const float period = GetPeriodSeconds();

		if (!clip.loop)
			return std::clamp(seconds, 0.0f, period);

		float wrapped = std::fmod(seconds, period);
		if (wrapped < 0.0f)
			wrapped += period;
		return wrapped;
	}
}
