#pragma once

namespace assetlib::chunk
{
	/**
	 * The chunks one read asked for: every payload in a single allocation, addressed by id.
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
			uint32_t id;
			uint32_t offset;  // into the payload buffer
			uint32_t size;
		};

		ChunkData() = default;
		ChunkData(std::vector<std::byte> bytes, std::vector<Slot> table) noexcept :
			m_Bytes(std::move(bytes)), m_Table(std::move(table))
		{}

		/** The payload for `id`, or empty when the container did not carry it. */
		[[nodiscard]] std::span<const std::byte>
		Get(uint32_t id) const noexcept
		{
			const Slot* slot = Find(id);
			return slot != nullptr ? std::span(m_Bytes).subspan(slot->offset, slot->size) :
			                         std::span<const std::byte>();
		}

		/** Distinct from an empty payload, which a chunk that was written empty has. */
		[[nodiscard]] bool
		Contains(uint32_t id) const noexcept
		{
			return Find(id) != nullptr;
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
	};
}
