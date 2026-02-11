#pragma once
#include "frame_graph/BindingSetView.h"

namespace gfx
{
	template <typename T>
	struct FrameGraphView
	{
		struct Desc
		{
			T* data = nullptr;

			Desc() noexcept = default;

			explicit Desc(T* ptr) : data(ptr) {}

			template <typename... Args>
			static Desc
			Create(Args&&... args)
			{
				return Desc(new T(std::forward<Args>(args)...));
			}
		};

		T* stableStorage = nullptr;

		FrameGraphView() = default;

		~FrameGraphView()
		{
			if (stableStorage)
			{
				delete stableStorage;
				stableStorage = nullptr;
			}
		}

		FrameGraphView(FrameGraphView&& other) noexcept : stableStorage(other.stableStorage)
		{
			other.stableStorage = nullptr;
		}

		FrameGraphView&
		operator=(FrameGraphView&& other) noexcept
		{
			if (this != &other)
			{
				if (stableStorage)
				{
					delete stableStorage;
				}
				stableStorage       = other.stableStorage;
				other.stableStorage = nullptr;
			}
			return *this;
		}

		FrameGraphView(const FrameGraphView&) = delete;
		FrameGraphView&
		operator=(const FrameGraphView&) = delete;

		void
		create(Desc desc, void* ctx)
		{
			if (stableStorage)
			{
				delete stableStorage;
			}

			stableStorage = desc.data;

			if (!stableStorage)
			{
				stableStorage = new T();
			}
		}

		void
		reinit(const T& newValue)
		{
			if (stableStorage)
			{
				*stableStorage = newValue;
			}
		}

		void
		destroy(const Desc&, void*)
		{
			if (stableStorage)
			{
				delete stableStorage;
				stableStorage = nullptr;
			}
		}

		operator const T&() const { return *stableStorage; }

		const T&
		Get() const noexcept
		{
			return *stableStorage;
		}

		template <typename... Args>
		void
		SetValue(Args&&... args)
		{
			if (stableStorage)
			{
				delete stableStorage;
			}
			stableStorage = new T(std::forward<Args>(args)...);
		}
	};

	using FGBindingSet = FrameGraphView<BindingSetView>;
	using FGCount      = FrameGraphView<uint32_t>;
}
