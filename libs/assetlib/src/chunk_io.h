#pragma once
#include <core/file/IFileSystem.h>

#include "ChunkData.h"
#include "IRangeReader.h"
#include <core/err/util.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <schema/ElementView.h>
#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>

namespace assetlib::chunk
{
	/**
	 * The container format `.bmesh`, `.bskel`, `.banim` and `.bvat` share: a fixed header, a run of
	 * 16-byte-aligned chunks, and a chunk table at the end. Chunks are addressed by id, so an
	 * absent one is not a malformed file -- adding data leaves what is already on disk readable.
	 *
	 * Chunk 0 is the file's schema: every struct its chunks hold, by name, and each entry says
	 * which layout (or which scalar) its elements are. A reader converts a chunk from the layout
	 * the file stores to the one the engine wants, by field name, so a file written before a
	 * struct changed shape still reads. The version in the header is a label a human can tell
	 * files apart by; the one thing it decides is that a file newer than the reader is refused.
	 */

	inline constexpr size_t   c_Align       = 16;
	inline constexpr uint32_t c_SchemaChunk = 0;  // reserved: no container may use id 0

	/** A container's scoped ChunkId enum; the id stored on disk is its underlying value. */
	template <typename Id>
	concept ChunkIdType = std::is_enum_v<Id> && std::same_as<std::underlying_type_t<Id>, uint32_t>;

	struct Header
	{
		uint32_t magic;
		uint16_t versionMajor;
		uint16_t versionMinor;
		uint8_t  byteOrder;  // 0 == little-endian
		uint8_t  pad[3];
		uint32_t chunkCount;
		uint32_t chunkTableOffset;
		uint64_t fileSize;
	};

	static_assert(sizeof(Header) == 32);

	struct Entry
	{
		uint32_t id;
		uint32_t elementSize;
		uint64_t offset;
		uint64_t byteSize;
		uint32_t layoutIndex;  // into the schema chunk, or schema::c_NoLayout for a run of values
		uint8_t  valueType;    // schema::ValueType of a value run; kNone for structs
		uint8_t  pad[3];
	};

	static_assert(sizeof(Entry) == 32);

	/** What one chunk's elements are: a struct of the schema, or a run of scalars. */
	struct ElementKind
	{
		uint32_t          layoutIndex      = schema::c_NoLayout;
		schema::ValueType valueType        = schema::ValueType::kNone;
		uint32_t          valuesPerElement = 1;  // a glm::mat4 element is 16 f32
	};

	/**
	 * The kind a C++ element type has under `schema`.
	 * @throws std::runtime_error if T is a struct the schema does not hold -- a POD nobody
	 *         registered a layout for.
	 */
	template <ChunkElement T>
	[[nodiscard]] ElementKind
	kindOf(const schema::Schema& schema, std::string_view what)
	{
		using Traits = schema::FieldTraits<std::remove_cvref_t<T>>;
		ElementKind kind;
		if constexpr (!std::is_void_v<typename Traits::Struct>)
		{
			static_assert(Traits::c_Count == 1, "an element is one struct, not an array of them");
			const auto index = schema.FindIndex(typeid(typename Traits::Struct));
			if (!index)
				core::throw_runtime_error(
					"{}: a chunk of {} has no layout in the schema",
					what,
					typeid(T).name());
			kind.layoutIndex = *index;
		}
		else
		{
			kind.valueType        = Traits::c_ValueType;
			kind.valuesPerElement = Traits::c_Count;
		}
		return kind;
	}

	/**
	 * `bytes`, an entry's payload as the file stores it, as elements of the engine's kind: a copy
	 * when the stored and wanted shapes agree, otherwise converted field by field.
	 *
	 * @throws std::runtime_error naming the container, the layout and the field when a shape cannot
	 *         become the wanted one, or if the entry does not agree with the stored schema.
	 */
	[[nodiscard]] std::vector<std::byte>
	readElements(
		const schema::Schema&      stored,
		const Entry&               entry,
		std::span<const std::byte> bytes,
		const schema::Schema&      current,
		const ElementKind&         wanted,
		size_t                     wantedElementSize,
		std::string_view           what);

	template <ChunkElement T>
	[[nodiscard]] std::vector<T>
	readElementsAs(
		const schema::Schema&      stored,
		const Entry&               entry,
		std::span<const std::byte> bytes,
		const schema::Schema&      current,
		std::string_view           what)
	{
		const auto out =
			readElements(stored, entry, bytes, current, kindOf<T>(current, what), sizeof(T), what);
		std::vector<T> values(out.size() / sizeof(T));
		std::copy_n(out.data(), out.size(), reinterpret_cast<std::byte*>(values.data()));
		return values;
	}

	/** Accumulates chunks behind a placeholder header, which Finish patches. */
	class Writer
	{
	public:
		/** The schema is chunk 0, written first; every Add resolves its element type against it. */
		explicit Writer(const schema::Schema& schema);

		template <ChunkElement T, ChunkIdType Id>
		void
		Add(Id id, std::span<const T> values)
		{
			Add(static_cast<uint32_t>(id), values);
		}

		template <ChunkElement T, ChunkIdType Id>
		void
		Add(Id id, const std::vector<T>& values)
		{
			Add(static_cast<uint32_t>(id), std::span<const T>(values));
		}

		template <ChunkElement T>
		void
		Add(uint32_t id, const std::vector<T>& values)
		{
			Add(id, std::span<const T>(values));
		}

		/** @throws std::runtime_error on id 0, or a struct T the schema does not hold. */
		template <ChunkElement T>
		void
		Add(uint32_t id, std::span<const T> values)
		{
			AddBytes(
				id,
				kindOf<T>(*m_Schema, "chunk writer"),
				static_cast<uint32_t>(sizeof(T)),
				std::as_bytes(values));
		}

		[[nodiscard]] std::vector<std::byte>
		Finish(uint32_t magic, uint16_t versionMajor, uint16_t versionMinor);

	private:
		void
		AddBytes(
			uint32_t                   id,
			const ElementKind&         kind,
			uint32_t                   elementSize,
			std::span<const std::byte> bytes);

		const schema::Schema* m_Schema;
		core::io::ByteWriter  m_Bytes;
		std::vector<Entry>    m_Chunks;
	};

	/** A byte stream's validated header, schema and chunk table, with the stream it indexes. */
	class Reader
	{
	public:
		/**
		 * @param what Container name the error messages are prefixed with, e.g. "bmesh".
		 * @param current The engine's schema, that every Read converts into.
		 * @throws std::runtime_error on bad magic, a version newer than `versionMajor`, a file that
		 *         predates the schema chunk, an unsupported byte order, or a stream shorter than the
		 *         header claims.
		 */
		Reader(
			std::span<const std::byte> bytes,
			uint32_t                   magic,
			uint16_t                   versionMajor,
			std::string_view           what,
			const schema::Schema&      current);

		/** The chunk's elements, or an empty vector when the container does not carry it. */
		template <ChunkElement T, ChunkIdType Id>
		[[nodiscard]] std::vector<T>
		Read(Id id) const
		{
			return Read<T>(static_cast<uint32_t>(id));
		}

		template <ChunkElement T>
		[[nodiscard]] std::vector<T>
		Read(uint32_t id) const
		{
			const Entry* entry = Find(id);
			return entry == nullptr ? std::vector<T>() : ReadEntry<T>(*entry);
		}

		/** @throws std::runtime_error if the chunk is absent. */
		template <ChunkElement T, ChunkIdType Id>
		[[nodiscard]] std::vector<T>
		Require(Id id) const
		{
			return Require<T>(static_cast<uint32_t>(id));
		}

		template <ChunkElement T>
		[[nodiscard]] std::vector<T>
		Require(uint32_t id) const
		{
			const Entry* entry = Find(id);
			if (entry == nullptr)
				core::throw_runtime_error("{}: missing required chunk", m_What);
			return ReadEntry<T>(*entry);
		}

		template <ChunkIdType Id>
		[[nodiscard]] bool
		Contains(Id id) const noexcept
		{
			return Find(static_cast<uint32_t>(id)) != nullptr;
		}

		/** The layouts the file was written with -- what a hook's predicate reads. */
		[[nodiscard]] const schema::Schema&
		GetStoredSchema() const noexcept
		{
			return m_Stored;
		}

		[[nodiscard]] uint16_t
		GetVersionMajor() const noexcept
		{
			return m_Header.versionMajor;
		}

		[[nodiscard]] uint16_t
		GetVersionMinor() const noexcept
		{
			return m_Header.versionMinor;
		}

		/**
		 * A struct chunk's elements as the file stores them, readable by field name -- what a hook
		 * recovers a value from.
		 * @throws std::runtime_error if the chunk is absent or holds values rather than structs.
		 */
		template <ChunkIdType Id>
		[[nodiscard]] schema::ElementView
		View(Id id) const
		{
			return View(static_cast<uint32_t>(id));
		}

		[[nodiscard]] schema::ElementView
		View(uint32_t id) const;

	private:
		template <ChunkElement T>
		[[nodiscard]] std::vector<T>
		ReadEntry(const Entry& entry) const
		{
			return readElementsAs<T>(m_Stored, entry, Payload(entry), *m_Current, m_What);
		}

		[[nodiscard]] const Entry*
		Find(uint32_t id) const noexcept;

		[[nodiscard]] std::span<const std::byte>
		Payload(const Entry& entry) const;

		std::span<const std::byte> m_Bytes;
		Header                     m_Header{};
		std::vector<Entry>         m_Table;
		schema::Schema             m_Stored;
		const schema::Schema*      m_Current;
		std::string_view           m_What;
	};

	/**
	 * A meaning change, as code: the one thing the by-name conversion cannot do is know what a
	 * value the new shape does not carry should become. `applies` reads the file's schema -- "this
	 * file's SourceStamp still has an mtime" -- never its version, so a sibling branch's file that
	 * carries the old shape under a newer number is still repaired. `run` sees the file through
	 * the reader and the converted result, and fixes what it must.
	 */
	/** The in-memory form of a container -- what a Reader assembles and a hook repairs. */
	template <typename T>
	concept LoadedContainer = std::is_class_v<T> && !std::is_trivially_copyable_v<T>;

	template <LoadedContainer T>
	struct Hook
	{
		std::function<bool(const schema::Schema& stored)>   applies;
		std::function<void(const Reader& reader, T& value)> run;
	};

	/** Every hook whose predicate holds, in order. */
	template <LoadedContainer T>
	void
	applyHooks(std::span<const Hook<T>> hooks, const Reader& reader, T& value)
	{
		for (const Hook<T>& hook : hooks)
			if (hook.applies(reader.GetStoredSchema()))
				hook.run(reader, value);
	}

	/**
	 * The bytes of the named chunks of `source`, and nothing else: the header, the chunk table,
	 * the schema and each requested chunk are the only reads. A survey of every container in a
	 * project has to come through here -- the chunks it wants are a few hundred bytes of a file
	 * of many megabytes, and a reader that fetched whole files would turn that survey into a full
	 * read of the project.
	 *
	 * Chunks the source does not carry are simply absent from the result.
	 *
	 * @throws std::runtime_error if the source cannot be read, or is malformed.
	 */
	[[nodiscard]] ChunkData
	readChunks(
		IRangeReader&             source,
		uint32_t                  magic,
		uint16_t                  versionMajor,
		std::span<const uint32_t> ids,
		std::string_view          what);

	/** readChunks against a file on disk: one open, one seek per chunk. */
	[[nodiscard]] ChunkData
	readChunksFromFile(
		const std::filesystem::path& path,
		uint32_t                     magic,
		uint16_t                     versionMajor,
		std::span<const uint32_t>    ids,
		std::string_view             what);

	/** readChunks against a mounted filesystem, which may be an archive. */
	[[nodiscard]] ChunkData
	readChunksFrom(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       magic,
		uint16_t                       versionMajor,
		std::span<const uint32_t>      ids,
		std::string_view               what);

	/** A list of strings as one blob of NUL-terminated bytes (one terminator per string). */
	[[nodiscard]] std::vector<char>
	packStrings(std::span<const std::string> strings);

	[[nodiscard]] std::vector<std::string>
	unpackStrings(std::span<const char> pool);

	template <ChunkElement T>
	std::vector<T>
	ChunkData::Read(uint32_t id, const schema::Schema& current) const
	{
		const Slot* slot = Find(id);
		if (slot == nullptr)
			return {};

		Entry entry{};
		entry.id          = slot->id;
		entry.elementSize = slot->elementSize;
		entry.byteSize    = slot->size;
		entry.layoutIndex = slot->layoutIndex;
		entry.valueType   = static_cast<uint8_t>(slot->valueType);
		return readElementsAs<T>(m_Stored, entry, Get(id), current, m_What);
	}
}
