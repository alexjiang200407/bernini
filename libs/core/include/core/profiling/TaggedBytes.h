#pragma once

#include <core/profiling/memory.h>

namespace core::profiling
{
	/**
	 * Charges `bytes` to a tag for as long as this object lives, and releases them when it dies.
	 * Hold one beside the allocation it accounts for — as a member of whatever owns the buffer —
	 * so the release cannot be forgotten on a path that throws or returns early.
	 *
	 * Re-seat rather than adjust: assigning a fresh `TaggedBytes` releases the old charge and takes
	 * the new one, which is what a container that grew wants.
	 *
	 * Cheap enough for a door and far too expensive for a per-element loop: two relaxed atomics and,
	 * where profiling is compiled in, a Tracy pool event. Tag whole containers, never their entries.
	 *
	 * **A charge is move-only, so whatever holds one is too** — and MSVC's `/Wall` makes an
	 * implicitly deleted copy constructor an error, so a holder declares all five special members
	 * rather than letting them be deduced.
	 */
	template <MemoryTagEnum Tag>
	class TaggedBytes
	{
	public:
		TaggedBytes() noexcept = default;

		TaggedBytes(const Tag tag, const std::size_t bytes) noexcept :
			m_Tag(tag), m_Bytes(bytes), m_Id(detail::mint_allocation_id())
		{
			detail::table_for<Tag>().Charge(static_cast<std::size_t>(tag), bytes);
			detail::tracy_alloc(m_Id, bytes, MemoryTagName(tag));
		}

		~TaggedBytes() noexcept { Release(); }

		TaggedBytes(const TaggedBytes&) = delete;
		TaggedBytes&
		operator=(const TaggedBytes&) = delete;

		TaggedBytes(TaggedBytes&& other) noexcept :
			m_Tag(other.m_Tag), m_Bytes(other.m_Bytes), m_Id(std::exchange(other.m_Id, 0))
		{}

		TaggedBytes&
		operator=(TaggedBytes&& other) noexcept
		{
			if (this == &other)
				return *this;

			Release();

			m_Tag   = other.m_Tag;
			m_Bytes = other.m_Bytes;
			m_Id    = std::exchange(other.m_Id, 0);

			return *this;
		}

		[[nodiscard]] std::size_t
		Bytes() const noexcept
		{
			return m_Bytes;
		}

	private:
		void
		Release() noexcept
		{
			if (m_Id == 0)
				return;

			detail::tracy_free(m_Id, MemoryTagName(m_Tag));
			detail::table_for<Tag>().Discharge(static_cast<std::size_t>(m_Tag), m_Bytes);

			m_Id = 0;
		}

		Tag         m_Tag   = Tag{};
		std::size_t m_Bytes = 0;

		// Tracy keys a pool allocation on an address, and this never has one: a charge accounts for
		// a container that may reallocate under it, so its data pointer would be freed at an
		// address Tracy never saw allocated. A minted id is unique, survives a move, and is the one
		// spelling a call site cannot get wrong. Zero means this holds no charge.
		uint64_t m_Id = 0;
	};
}
