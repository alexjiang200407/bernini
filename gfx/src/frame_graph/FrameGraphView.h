#pragma once

namespace gfx
{
	template <typename T>
	struct FrameGraphView
	{
		struct Desc
		{
			T const* val;
			Desc() noexcept = default;
			Desc(const T& data) : val(std::addressof(data)) {}
		};

		void
		create(const Desc& desc, void* ctx)
		{
			val = desc.val;
		}

		void
		destroy(const Desc& desc, void*)
		{
			val = desc.val;
		}

		FrameGraphView()                 = default;
		FrameGraphView(FrameGraphView&&) = default;

		operator const T&() { return *val; }

		const T&
		Get() const noexcept
		{
			return *val;
		}

		T const* val;
	};
}
