#include "chunk_io.h"

#include "CheckedFileReader.h"
#include "MountedFileReader.h"
#include "fs_util.h"

#include <core/err/util.h>
#include <schema/convert.h>

namespace assetlib::chunk
{
	using core::throw_runtime_error;
	using core::io::ByteReader;

	namespace
	{
		void
		checkHeader(
			const Header&    header,
			uint32_t         magic,
			uint16_t         versionMajor,
			std::string_view what)
		{
			if (header.magic != magic)
				throw_runtime_error("{}: bad magic", what);
			if (header.versionMajor > versionMajor)
				throw_runtime_error(
					"{}: written by a newer engine (format {}; this build reads up to {})",
					what,
					header.versionMajor,
					versionMajor);
			if (header.byteOrder != 0)
				throw_runtime_error("{}: unsupported byte order", what);
		}

		void
		checkEntry(const Entry& entry, uint64_t streamSize, std::string_view what)
		{
			if (entry.elementSize == 0)
				throw_runtime_error("{}: chunk {} has zero-byte elements", what, entry.id);
			if (entry.byteSize % entry.elementSize != 0)
				throw_runtime_error(
					"{}: chunk byte size is not a multiple of the element size",
					what);
			// Subtraction rather than addition: a crafted offset near UINT64_MAX would wrap past the sum.
			if (entry.byteSize > streamSize || entry.offset > streamSize - entry.byteSize)
				throw_runtime_error("{}: chunk extends past end of stream", what);
		}

		/** The one message a file from before the schema chunk gets, wherever it is opened. */
		[[noreturn]] void
		throwPredatesSchema(uint16_t versionMajor, std::string_view what)
		{
			throw_runtime_error(
				"{}: format {} predates the schema table and cannot be read; re-bake it",
				what,
				versionMajor);
		}

		/** The layout an entry's elements are stored as, checked against the entry's own claim. */
		schema::LayoutRef
		storedLayout(const schema::Schema& stored, const Entry& entry, std::string_view what)
		{
			if (entry.layoutIndex >= stored.GetLayouts().size())
				throw_runtime_error(
					"{}: chunk {} names layout {} which the file's schema does not hold",
					what,
					entry.id,
					entry.layoutIndex);
			const schema::LayoutRef layout(stored, entry.layoutIndex);
			if (entry.elementSize != layout.GetLayout().size)
				throw_runtime_error(
					"{}: chunk {} says {}-byte elements but its layout {} is {} bytes",
					what,
					entry.id,
					entry.elementSize,
					layout.GetLayout().name,
					layout.GetLayout().size);
			return layout;
		}
	}

	std::vector<std::byte>
	readElements(
		const schema::Schema&      stored,
		const Entry&               entry,
		std::span<const std::byte> bytes,
		const schema::Schema&      current,
		const ElementKind&         wanted,
		size_t                     wantedElementSize,
		std::string_view           what)
	{
		const bool storedStructs = entry.layoutIndex != schema::c_NoLayout;
		const bool wantedStructs = wanted.layoutIndex != schema::c_NoLayout;

		try
		{
			if (storedStructs != wantedStructs)
			{
				const std::string storedShape =
					storedStructs ?
						"struct " + stored.GetLayout(entry.layoutIndex).name :
						std::string(
							schema::valueTypeName(static_cast<schema::ValueType>(entry.valueType)));
				const std::string wantedShape =
					wantedStructs ? "struct " + current.GetLayout(wanted.layoutIndex).name :
									std::string(schema::valueTypeName(wanted.valueType));
				throw_runtime_error(
					"chunk {}: file stores {}, engine wants {}, no conversion",
					entry.id,
					storedShape,
					wantedShape);
			}

			if (storedStructs)
			{
				const schema::LayoutRef from = storedLayout(stored, entry, what);
				const schema::LayoutRef to(current, wanted.layoutIndex);
				if (schema::sameLayout(from, to))
					return std::vector<std::byte>(bytes.begin(), bytes.end());
				return schema::convert(from, bytes, to);
			}

			const auto storedType = static_cast<schema::ValueType>(entry.valueType);
			if (storedType >= schema::ValueType::kNone)
				throw_runtime_error("chunk {}: unknown value type", entry.id);
			const auto storedValueSize = schema::valueSize(storedType);
			if (entry.elementSize % storedValueSize != 0)
				throw_runtime_error(
					"chunk {}: {}-byte elements of {}",
					entry.id,
					entry.elementSize,
					schema::valueTypeName(storedType));
			if (entry.elementSize / storedValueSize != wanted.valuesPerElement)
				throw_runtime_error(
					"chunk {}: file stores {}[{}] elements, engine wants {}[{}]",
					entry.id,
					schema::valueTypeName(storedType),
					entry.elementSize / storedValueSize,
					schema::valueTypeName(wanted.valueType),
					wanted.valuesPerElement);

			auto out = schema::convertValues(storedType, bytes, wanted.valueType);
			assert(out.size() % wantedElementSize == 0);
			return out;
		}
		catch (const std::runtime_error& error)
		{
			throw_runtime_error("{}: {}", what, error.what());
		}
	}

	Writer::Writer(const schema::Schema& schema) : m_Schema(&schema)
	{
		m_Bytes.WritePod(Header{});

		ElementKind kind;
		kind.valueType = schema::ValueType::kU8;
		AddBytes(c_SchemaChunk, kind, 1, schema::serialize(schema));
	}

	void
	Writer::AddBytes(
		uint32_t                   id,
		const ElementKind&         kind,
		uint32_t                   elementSize,
		std::span<const std::byte> bytes)
	{
		if (id == c_SchemaChunk && !m_Chunks.empty())
			throw_runtime_error("chunk writer: id 0 is the schema's");

		m_Bytes.AlignTo(c_Align);

		Entry entry{};
		entry.id          = id;
		entry.elementSize = elementSize;
		entry.offset      = m_Bytes.Size();
		entry.byteSize    = bytes.size();
		entry.layoutIndex = kind.layoutIndex;
		entry.valueType   = static_cast<uint8_t>(kind.valueType);
		m_Bytes.WriteBytes(bytes);
		m_Chunks.push_back(entry);
	}

	std::vector<std::byte>
	Writer::Finish(uint32_t magic, uint16_t versionMajor, uint16_t versionMinor)
	{
		m_Bytes.AlignTo(c_Align);
		const auto tableOffset = m_Bytes.Size();
		m_Bytes.WritePodArray(std::span<const Entry>(m_Chunks));

		Header header{};
		header.magic            = magic;
		header.versionMajor     = versionMajor;
		header.versionMinor     = versionMinor;
		header.byteOrder        = 0;
		header.chunkCount       = static_cast<uint32_t>(m_Chunks.size());
		header.chunkTableOffset = static_cast<uint32_t>(tableOffset);
		header.fileSize         = m_Bytes.Size();
		m_Bytes.PatchPod(0, header);

		return m_Bytes.Take();
	}

	Reader::Reader(
		std::span<const std::byte> bytes,
		uint32_t                   magic,
		uint16_t                   versionMajor,
		std::string_view           what,
		const schema::Schema&      current) : m_Bytes(bytes), m_Current(&current), m_What(what)
	{
		ByteReader reader(bytes);
		m_Header = reader.ReadPod<Header>();

		checkHeader(m_Header, magic, versionMajor, what);

		if (m_Header.fileSize > bytes.size())
			throw_runtime_error("{}: stream shorter than declared file size", what);

		const auto tableBytes = static_cast<size_t>(m_Header.chunkCount) * sizeof(Entry);
		if (tableBytes > bytes.size() || m_Header.chunkTableOffset > bytes.size() - tableBytes)
			throw_runtime_error("{}: chunk table extends past end of stream", what);

		reader.Seek(m_Header.chunkTableOffset);
		m_Table.resize(m_Header.chunkCount);
		for (Entry& entry : m_Table)
		{
			entry = reader.ReadPod<Entry>();
			checkEntry(entry, bytes.size(), what);
		}

		const Entry* schemaEntry = Find(c_SchemaChunk);
		if (schemaEntry == nullptr)
			throwPredatesSchema(m_Header.versionMajor, what);
		try
		{
			m_Stored = schema::deserialize(Payload(*schemaEntry));
		}
		catch (const std::runtime_error& error)
		{
			throw_runtime_error("{}: {}", what, error.what());
		}
	}

	const Entry*
	Reader::Find(uint32_t id) const noexcept
	{
		const auto it = std::ranges::find(m_Table, id, &Entry::id);
		return it == m_Table.end() ? nullptr : &*it;
	}

	std::span<const std::byte>
	Reader::Payload(const Entry& entry) const
	{
		return m_Bytes.subspan(entry.offset, entry.byteSize);
	}

	schema::ElementView
	Reader::View(uint32_t id) const
	{
		const Entry* entry = Find(id);
		if (entry == nullptr)
			throw_runtime_error("{}: no chunk {} to view", m_What, id);
		if (entry->layoutIndex == schema::c_NoLayout)
			throw_runtime_error("{}: chunk {} holds values, not structs", m_What, id);
		return schema::ElementView(storedLayout(m_Stored, *entry, m_What), Payload(*entry));
	}

	ChunkData
	readChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint16_t                     versionMajor,
		std::span<const uint32_t>    ids,
		std::string_view             what)
	{
		CheckedFileReader source(path, what);
		return readChunks(source, magic, versionMajor, ids, what);
	}

	ChunkData
	readChunksFrom(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       magic,
		uint16_t                       versionMajor,
		std::span<const uint32_t>      ids,
		std::string_view               what)
	{
		MountedFileReader source(fileSystem, path, what);
		return readChunks(source, magic, versionMajor, ids, what);
	}

	ChunkData
	readChunks(
		IRangeReader&             source,
		uint32_t                  magic,
		uint16_t                  versionMajor,
		std::span<const uint32_t> ids,
		std::string_view          what)
	{
		Header header{};
		source.ReadAt(&header, sizeof(header), 0);
		checkHeader(header, magic, versionMajor, what);

		const uint64_t tableBytes = static_cast<uint64_t>(header.chunkCount) * sizeof(Entry);
		source.CheckRange(tableBytes, header.chunkTableOffset);

		std::vector<Entry> table(header.chunkCount);
		if (!table.empty())
			source.ReadAt(table.data(), tableBytes, header.chunkTableOffset);

		const auto wanted = [&](uint32_t id) {
			return id == c_SchemaChunk || std::ranges::find(ids, id) != ids.end();
		};

		// Sized before anything is read, so the payloads land in one buffer rather than one
		// allocation each.
		std::vector<ChunkData::Slot> slots;
		uint64_t                     total = 0;
		for (const Entry& entry : table)
		{
			if (!wanted(entry.id))
				continue;

			checkEntry(entry, source.GetSize(), what);
			source.CheckRange(entry.byteSize, entry.offset);

			slots.push_back(
				{ entry.id,
			      static_cast<uint32_t>(total),
			      static_cast<uint32_t>(entry.byteSize),
			      entry.elementSize,
			      entry.layoutIndex,
			      static_cast<schema::ValueType>(entry.valueType) });
			total += entry.byteSize;
		}

		std::vector<std::byte> bytes(total);
		for (size_t i = 0; i < slots.size(); ++i)
		{
			if (slots[i].size == 0)
				continue;

			const Entry& entry = *std::ranges::find(table, slots[i].id, &Entry::id);
			source.ReadAt(bytes.data() + slots[i].offset, slots[i].size, entry.offset);
		}

		const auto schemaSlot = std::ranges::find(slots, c_SchemaChunk, &ChunkData::Slot::id);
		if (schemaSlot == slots.end())
			throwPredatesSchema(header.versionMajor, what);

		schema::Schema stored;
		try
		{
			stored =
				schema::deserialize(std::span(bytes).subspan(schemaSlot->offset, schemaSlot->size));
		}
		catch (const std::runtime_error& error)
		{
			throw_runtime_error("{}: {}", what, error.what());
		}

		return ChunkData(std::move(bytes), std::move(slots), std::move(stored), what);
	}

	std::vector<char>
	packStrings(std::span<const std::string> strings)
	{
		std::vector<char> pool;
		for (const auto& s : strings)
		{
			pool.insert(pool.end(), s.begin(), s.end());
			pool.push_back('\0');
		}
		return pool;
	}

	std::vector<std::string>
	unpackStrings(std::span<const char> pool)
	{
		std::vector<std::string> out;
		std::string              current;
		for (const char c : pool)
		{
			if (c == '\0')
			{
				out.push_back(current);
				current.clear();
			}
			else
			{
				current.push_back(c);
			}
		}
		return out;
	}
}
