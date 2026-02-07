#pragma once

namespace gfx
{
	template <typename T>
	struct FrameGraphView
	{
		struct Desc
		{
			std::shared_ptr<T> data;

			Desc() noexcept = default;

			Desc(std::shared_ptr<T> ptr) : data(std::move(ptr)) {}

			template <typename... Args>
			static Desc
			Create(Args&&... args)
			{
				return Desc(std::make_shared<T>(std::forward<Args>(args)...));
			}
		};

		void
		create(Desc desc, void* ctx)
		{
			stableStorage = std::move(desc.data);

			if (!stableStorage)
			{
				stableStorage = std::make_shared<T>();
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
			stableStorage.reset();
		}

		FrameGraphView()                          = default;
		FrameGraphView(FrameGraphView&&) noexcept = default;
		FrameGraphView&
		operator=(FrameGraphView&&) noexcept = default;

		operator const T&() const { return *stableStorage; }
		const T&
		Get() const noexcept
		{
			return *stableStorage;
		}

		template <typename... Args>
		void
		SetValue(Args&&... args) noexcept
		{
			stableStorage = std::make_shared<T>(std::forward<Args>(args)...);
		}

		void
		SetValue(std::shared_ptr<T> ptr) noexcept
		{
			stableStorage = std::move(ptr);
		}

		std::shared_ptr<T> stableStorage;
	};

	using FGBindingSet = FrameGraphView<nvrhi::BindingSetHandle>;
	using FGCount      = FrameGraphView<uint32_t>;
}
