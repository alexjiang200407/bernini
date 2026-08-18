#pragma once
#include <core/type_traits.h>
#include <schema/Schema.h>

namespace assetlib::chunk
{
	struct Entry;

	/** What a chunk holds: a run of PODs, copied in and out by bytes. */
	template <typename T>
	concept ChunkElement = core::type_traits::trivially_copyable<T>;

	/**
	 * The chunks one read asked for: every payload in a single allocation, addressed by id, with
	 * the schema they were written under so each reads as the engine's shape.
	 *
	 * A read names a handful of ids and every caller looks each one up once, so the table is scanned
	 * rather than hashed and the payloads share one buffer instead of a heap block each. The chunks
	 * reached this way are the small ones -- reference lists, tables, stamps -- because a whole
	 * container is read whole, not chunk by chunk.
	 */
	class ChunkData
	{
	public:
		struct Slot
		{
			uint32_t          id;
			uint32_t          offset;  // into the payload buffer
			uint32_t          size;
			uint32_t          elementSize;
			uint32_t          layoutIndex;
			schema::ValueType valueType;
		};

		ChunkData() = default;
		ChunkData(
			std::vector<std::byte> bytes,
			std::vector<Slot>      table,
			schema::Schema         stored,
			std::string_view       what) noexcept :
			m_Bytes(std::move(bytes)), m_Table(std::move(table)), m_Stored(std::move(stored)),
			m_What(what)
		{}

		/** The payload for `id` as the file stores it, or empty when the container did not carry it. */
		[[nodiscard]] std::span<const std::byte>
		Get(uint32_t id) const noexcept
		{
			const Slot* slot = Find(id);
			return slot != nullptr ? std::span(m_Bytes).subspan(slot->offset, slot->size) :
			                         std::span<const std::byte>();
		}

		/**
		 * The payload for `id` as `current`'s elements -- converted from the stored layout where
		 * the two differ -- or empty when the container did not carry it. See chunk::readElements.
		 */
		template <ChunkElement T>
		[[nodiscard]] std::vector<T>
		Read(uint32_t id, const schema::Schema& current) const;

		/** Distinct from an empty payload, which a chunk that was written empty has. */
		[[nodiscard]] bool
		Contains(uint32_t id) const noexcept
		{
			return Find(id) != nullptr;
		}

		[[nodiscard]] const schema::Schema&
		GetStoredSchema() const noexcept
		{
			return m_Stored;
		}

	private:
		[[nodiscard]] const Slot*
		Find(uint32_t id) const noexcept
		{
			const auto found = std::ranges::find(m_Table, id, &Slot::id);
			return found != m_Table.end() ? &*found : nullptr;
		}

		std::vector<std::byte> m_Bytes;
		std::vector<Slot>      m_Table;
		schema::Schema         m_Stored;
		std::string            m_What;
	};
}
