#pragma once
#include "idl/idl.h"
#include "scene/RangeBuffer.h"

namespace bgl
{
	// The unit a raw arena allocates in. Its size is the alignment every record and every range
	// starts on, so a payload's float4 lands where a whole-struct load can reach it.
	struct RawBlock
	{
		uint32_t words[idl::cRawBlockBytes / sizeof(uint32_t)];
	};

	static_assert(sizeof(RawBlock) == idl::cRawBlockBytes);

	struct RawBufferDesc
	{
		// Where the arena starts, in bytes, rounded up to whole blocks. It grows on demand; the
		// reserved null record is carried on top of it, so a caller's budget is its own.
		uint32_t initialBytes = 0;

		// How much of the arena's head is reserved for the null record: cRawPayloadOffset plus the
		// largest payload any record of this arena holds. A null reference reads zeros for its
		// header *and* its payload only if this covers both, so an arena that under-declares it
		// hands a null dereference the first live record instead.
		uint32_t nullRecordBytes = idl::cRawPayloadOffset;

		// The dirty-upload granularity, forwarded to RangeBufferDesc::blockSize. Not the allocation
		// grid -- that is cRawBlockBytes and is not a caller's to choose.
		uint32_t uploadBlockBytes = 65536;

		std::string debugName;
	};

	/**
	 * A GPU-mirrored arena of bytes, read through a `RawBuffer` in Slang (types/RawBuffer.slang).
	 *
	 * Two things live in one: a **record**, which begins with an `idl::RecordHeader` naming its kind
	 * and is addressed by an `idl::RawEntry`; and a **range**, which is bytes with no header,
	 * addressed by an `idl::RawRange`, for data whose kind is already recorded elsewhere (a
	 * submesh's vertices, whose `VertexLayout` is the decode rule).
	 *
	 * Byte offset 0 is null for both, and the arena's head is reserved so a null dereference reads
	 * zeros rather than a live record.
	 *
	 * `Tag` is the arena's own kind enum, and `AddRecord` takes that enum and nothing else, so a
	 * caller cannot write a bare integer into a header. `void` is an arena of ranges alone, which
	 * has no kinds to name and no `AddRecord` to call.
	 */
	template <typename Tag = void>
		requires(std::is_void_v<Tag> || std::is_enum_v<Tag>)
	class RawBuffer
	{
	public:
		RawBuffer() noexcept = default;
		RawBuffer(RawBufferDesc desc, ResourceManagerRef resourceManager)
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		RawBuffer(const RawBuffer&)     = delete;
		RawBuffer(RawBuffer&&) noexcept = default;

		RawBuffer&
		operator=(const RawBuffer&) = delete;

		RawBuffer&
		operator=(RawBuffer&&) noexcept = default;

		void
		Init(RawBufferDesc desc, ResourceManagerRef resourceManager)
		{
			gassert(desc.initialBytes > 0, "RawBuffer must have a positive initial size");
			gassert(
				desc.nullRecordBytes >= idl::cRawPayloadOffset,
				"The null record must cover at least a header");

			m_NullRecordBlocks = ToBlockCount(desc.nullRecordBytes);

			RangeBufferDesc blockDesc;
			blockDesc.initialCount = ToBlockCount(desc.initialBytes) + m_NullRecordBlocks;
			blockDesc.blockSize    = desc.uploadBlockBytes;
			blockDesc.maxBytes     = c_MaxRawBufferBytes;
			blockDesc.isRaw        = true;
			blockDesc.debugName    = desc.debugName;

			m_Blocks.Init(std::move(blockDesc), std::move(resourceManager));

			ReserveNullRecord();
		}

		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Blocks.IsInitialized();
		}

		[[nodiscard]] uint64_t
		GetByteCapacity() const noexcept
		{
			return static_cast<uint64_t>(m_Blocks.Capacity()) * idl::cRawBlockBytes;
		}

		/**
		 * Writes a header naming `tag` followed by `payload`, and returns what addresses the header.
		 *
		 * @post the payload begins `idl::cRawPayloadOffset` bytes after the returned offset, and the
		 * gap between header and payload is zeroed.
		 * @throws std::runtime_error if the record would take the arena past what a raw view can
		 * address.
		 */
		template <typename T = Tag>
		[[nodiscard]] idl::RawEntry
		AddRecord(T tag, std::span<const std::byte> payload)
			requires(!std::is_void_v<T> && std::same_as<T, Tag>)
		{
			gassert(IsInitialized(), "RawBuffer is uninitialized; call Init() first");

			// ADR-6's invariant, and the one place the payload and the head's size meet: a record
			// bigger than the null record makes a null dereference read the first live one.
			gassert(
				idl::cRawPayloadOffset + payload.size() <= GetReservedBytes(),
				"A record payload larger than the null record this arena reserved");

			const auto handle = Allocate(MeasureRecord(payload));
			const auto record = m_Blocks.MutableRangeBytes(handle);

			auto header = idl::RecordHeader();
			header.type = static_cast<uint32_t>(tag);

			// The gap between header and payload, and the pad past it, stay zero: the mirror
			// value-initializes a block and re-zeroes one on erase, so every block handed out is
			// clean.
			std::memcpy(record.data(), &header, sizeof(header));

			if (!payload.empty())
			{
				std::memcpy(record.data() + idl::cRawPayloadOffset, payload.data(), payload.size());
			}

			return idl::RawEntry{ ToByteOffset(handle) };
		}

		/**
		 * Writes `bytes` with no header, for data whose kind is recorded by whatever names it.
		 *
		 * @throws std::runtime_error if the range would take the arena past what a raw view can
		 * address.
		 */
		[[nodiscard]] idl::RawRange
		AddBytes(std::span<const std::byte> bytes)
		{
			gassert(IsInitialized(), "RawBuffer is uninitialized; call Init() first");
			gassert(!bytes.empty(), "AddBytes requires a non-empty range");

			const auto handle = Allocate(MeasureRange(bytes));
			const auto range  = m_Blocks.MutableRangeBytes(handle);

			std::memcpy(range.data(), bytes.data(), bytes.size());

			return idl::RawRange{ ToByteOffset(handle) };
		}

		// The tag a record was written with. For a reader that has only the offset -- the CPU has no
		// business decoding a payload it wrote, but it does have to know which kind it is freeing.
		template <typename T = Tag>
		[[nodiscard]] T
		GetTagAt(uint32_t byteOffset) const
			requires(!std::is_void_v<T> && std::same_as<T, Tag>)
		{
			gassert(IsOffsetValid(byteOffset), "GetTagAt on an offset with no live record");

			auto header = idl::RecordHeader();
			std::memcpy(&header, &m_Blocks.AtIndex(ToBlockIndex(byteOffset)), sizeof(header));
			return static_cast<T>(header.type);
		}

		// False for anything inside the reserved head, an offset off the block grid, one the arena
		// never handed out, and one whose allocation has been freed. The head is a range like any
		// other to the buffer beneath, so guarding only offset 0 would leave the rest of the null
		// record erasable -- and a freed null record is one an ordinary allocation lands inside.
		[[nodiscard]] bool
		IsOffsetValid(uint32_t byteOffset) const noexcept
		{
			if (byteOffset < GetReservedBytes() || byteOffset % idl::cRawBlockBytes != 0)
			{
				return false;
			}
			return m_Blocks.IsIndexValid(ToBlockIndex(byteOffset));
		}

		// The largest an arena can grow to, which is what its view can address rather than what the
		// device could allocate.
		[[nodiscard]] static constexpr uint64_t
		GetByteCeiling() noexcept
		{
			return c_MaxRawBufferBytes;
		}

		void
		Erase(uint32_t byteOffset)
		{
			gassert(IsOffsetValid(byteOffset), "Erase on an offset with no live allocation");
			m_Blocks.EraseByIndex(ToBlockIndex(byteOffset));
		}

		void
		Update(ICommandList* cmdList)
		{
			m_Blocks.Update(cmdList);
		}

		// Re-read every frame: growth mints a new handle and retires the old one.
		[[nodiscard]] DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			return m_Blocks.GetDescriptorHandle();
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_Blocks.GetBufferHandle();
		}

		void
		Release(bool deferred = true) noexcept
		{
			m_Blocks.Release(deferred);
		}

	private:
		[[nodiscard]] static uint32_t
		ToBlockCount(uint64_t bytes) noexcept
		{
			return static_cast<uint32_t>((bytes + idl::cRawBlockBytes - 1) / idl::cRawBlockBytes);
		}

		[[nodiscard]] static uint32_t
		ToByteOffset(core::multi_slot_handle handle) noexcept
		{
			return handle.index * idl::cRawBlockBytes;
		}

		[[nodiscard]] static uint32_t
		ToBlockIndex(uint32_t byteOffset) noexcept
		{
			return byteOffset / idl::cRawBlockBytes;
		}

		[[nodiscard]] core::multi_slot_handle
		Allocate(uint32_t bytes)
		{
			return m_Blocks.AllocateRange(ToBlockCount(bytes));
		}

		// Held for the arena's lifetime and handed to nobody, so byte offset 0 stays null. The
		// blocks are already zero -- the mirror value-initializes them and AllocateRange marked
		// them dirty -- which is what makes a null dereference read zeros for a whole record.
		void
		ReserveNullRecord()
		{
			// RangeBuffer reserves element 0 itself; the rest of the null record is the arena's,
			// and it must be the very next block or a null read would run into a live one.
			if (m_NullRecordBlocks > 1)
			{
				const auto handle = m_Blocks.AllocateRange(m_NullRecordBlocks - 1);
				gassert(handle.index == 1, "The null record must own the head of the arena");
			}
		}

		// 64-bit throughout, and checked rather than asserted: the size is a caller's, and the wrap
		// it would otherwise take is exactly the one this arena exists to make loud.
		[[nodiscard]] uint32_t
		MeasureRecord(std::span<const std::byte> payload) const
		{
			return CheckByteSize(static_cast<uint64_t>(idl::cRawPayloadOffset) + payload.size());
		}

		[[nodiscard]] uint32_t
		MeasureRange(std::span<const std::byte> bytes) const
		{
			return CheckByteSize(bytes.size());
		}

		// The bound is what one allocation can reach, not what the buffer can hold: the reserved
		// head already owns the first blocks, so an allocation of the whole address space never
		// fits -- and it is the size that would truncate to nothing on the way to a block count.
		[[nodiscard]] uint32_t
		CheckByteSize(uint64_t bytes) const
		{
			const uint64_t allocatable = c_MaxRawBufferBytes - GetReservedBytes();

			if (bytes > allocatable)
			{
				core::throw_runtime_error(
					"A raw allocation of {} bytes is past the {} this arena can address",
					bytes,
					allocatable);
			}
			return static_cast<uint32_t>(bytes);
		}

		[[nodiscard]] uint32_t
		GetReservedBytes() const noexcept
		{
			return m_NullRecordBlocks * idl::cRawBlockBytes;
		}

		RangeBuffer<RawBlock> m_Blocks;
		uint32_t              m_NullRecordBlocks = 1;
	};
}
