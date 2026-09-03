#pragma once

#include <core/profiling/memory.h>

namespace core::profiling
{
	/**
	 * Charges `bytes` to a `MemoryTag` for as long as this object lives, and releases them when it
	 * dies. Hold one beside the allocation it accounts for -- as a member of whatever owns the
	 * buffer -- so the release cannot be forgotten on a path that throws or returns early.
	 *
	 * Re-seat rather than adjust: assigning a fresh `TaggedBytes` releases the old charge and takes
	 * the new one, which is what a container that grew wants.
	 *
	 * Cheap enough for a door and far too expensive for a per-element loop: two relaxed atomics and,
	 * where profiling is compiled in, a Tracy pool event. Tag whole containers, never their entries.
	 */
	class TaggedBytes
	{
	public:
		TaggedBytes() noexcept = default;

		TaggedBytes(MemoryTag tag, std::size_t bytes) noexcept;

		~TaggedBytes() noexcept;

		TaggedBytes(const TaggedBytes&) = delete;
		TaggedBytes&
		operator=(const TaggedBytes&) = delete;

		TaggedBytes(TaggedBytes&& other) noexcept;
		TaggedBytes&
		operator=(TaggedBytes&& other) noexcept;

		[[nodiscard]] std::size_t
		Bytes() const noexcept
		{
			return m_Bytes;
		}

		[[nodiscard]] MemoryTag
		Tag() const noexcept
		{
			return m_Tag;
		}

	private:
		void
		Release() noexcept;

		MemoryTag   m_Tag   = MemoryTag::kCount;
		std::size_t m_Bytes = 0;

		// Tracy keys a pool allocation on an address, and this never has one: a charge accounts for
		// a container that may reallocate under it, so its data pointer would be freed at an
		// address Tracy never saw allocated. A minted id is unique, survives a move, and is the one
		// spelling a call site cannot get wrong. Zero means this holds no charge.
		uint64_t m_Id = 0;
	};
}
