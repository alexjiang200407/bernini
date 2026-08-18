#include <assetlib_schema/Schema.h>

#include <core/err/util.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

namespace assetlib::schema
{
	using core::throw_runtime_error;

	size_t
	valueSize(ValueType valueType) noexcept
	{
		switch (valueType)
		{
		case ValueType::kU8:
		case ValueType::kI8:
			return 1;
		case ValueType::kU16:
		case ValueType::kI16:
			return 2;
		case ValueType::kU32:
		case ValueType::kI32:
		case ValueType::kF32:
			return 4;
		case ValueType::kU64:
		case ValueType::kI64:
		case ValueType::kF64:
			return 8;
		case ValueType::kNone:
		case ValueType::kCount:
			break;
		}
		assert(false && "a struct's size is its layout's");
		return 0;
	}

	std::string_view
	valueTypeName(ValueType valueType) noexcept
	{
		switch (valueType)
		{
		case ValueType::kU8:
			return "u8";
		case ValueType::kU16:
			return "u16";
		case ValueType::kU32:
			return "u32";
		case ValueType::kU64:
			return "u64";
		case ValueType::kI8:
			return "i8";
		case ValueType::kI16:
			return "i16";
		case ValueType::kI32:
			return "i32";
		case ValueType::kI64:
			return "i64";
		case ValueType::kF32:
			return "f32";
		case ValueType::kF64:
			return "f64";
		case ValueType::kNone:
			return "struct";
		case ValueType::kCount:
			break;
		}
		return "?";
	}

	uint32_t
	Schema::Add(Layout layout, std::type_index cppType)
	{
		if (layout.fields.empty())
			throw_runtime_error("schema: {} has no fields", layout.name);
		if (Find(layout.name) != nullptr)
			throw_runtime_error("schema: {} is registered twice", layout.name);

		for (const Field& field : layout.fields)
		{
			if (field.type >= Type::kCount)
				throw_runtime_error("schema: {}.{}: unknown type", layout.name, field.name);
			if (field.valueType >= ValueType::kCount)
				throw_runtime_error("schema: {}.{}: unknown value type", layout.name, field.name);
			if (field.count == 0)
				throw_runtime_error("schema: {}.{}: zero elements", layout.name, field.name);
			if ((field.type == Type::kArray) != (field.count != 1))
				throw_runtime_error(
					"schema: {}.{}: only an array has a count other than 1",
					layout.name,
					field.name);
			if (field.type == Type::kStruct && field.HoldsValues())
				throw_runtime_error(
					"schema: {}.{}: a struct field names its layout",
					layout.name,
					field.name);
			if (field.type == Type::kValue && !field.HoldsValues())
				throw_runtime_error(
					"schema: {}.{}: a value field names no layout",
					layout.name,
					field.name);
			if (field.HoldsValues() == (field.valueType == ValueType::kNone))
				throw_runtime_error(
					"schema: {}.{}: a field holds values or structs, never both or neither",
					layout.name,
					field.name);
			if (!field.HoldsValues() && field.layoutIndex >= m_Layouts.size())
				throw_runtime_error(
					"schema: {}.{}: names layout {} which is not registered before it",
					layout.name,
					field.name,
					field.layoutIndex);
			if (!field.defaultValue.empty() && field.defaultValue.size() != GetFieldSize(field))
				throw_runtime_error(
					"schema: {}.{}: default is {} bytes, the field is {}",
					layout.name,
					field.name,
					field.defaultValue.size(),
					GetFieldSize(field));
		}

		std::ranges::sort(layout.fields, {}, &Field::offset);

		size_t cursor    = 0;
		size_t alignment = 1;
		for (const Field& field : layout.fields)
		{
			const auto fieldAlignment = GetFieldAlignment(field);
			const auto size           = GetFieldSize(field);
			alignment                 = std::max(alignment, fieldAlignment);

			if (field.offset < cursor)
				throw_runtime_error(
					"schema: {}.{} at {} overlaps the field before it",
					layout.name,
					field.name,
					field.offset);
			if (field.offset - cursor >= fieldAlignment)
				throw_runtime_error(
					"schema: {}: {} bytes at {} belong to no field",
					layout.name,
					field.offset - cursor,
					cursor);
			if (field.offset % fieldAlignment != 0)
				throw_runtime_error(
					"schema: {}.{} at {} is not {}-aligned",
					layout.name,
					field.name,
					field.offset,
					fieldAlignment);
			if (field.offset + size > layout.size)
				throw_runtime_error(
					"schema: {}.{} ends at {}, past the layout's {} bytes",
					layout.name,
					field.name,
					field.offset + size,
					layout.size);
			cursor = field.offset + size;
		}
		if (layout.size - cursor >= alignment)
			throw_runtime_error(
				"schema: {}: {} bytes at {} belong to no field",
				layout.name,
				layout.size - cursor,
				cursor);

		for (size_t i = 0; i < layout.fields.size(); ++i)
			for (size_t j = i + 1; j < layout.fields.size(); ++j)
				if (layout.fields[i].name == layout.fields[j].name)
					throw_runtime_error(
						"schema: {}.{} is declared twice",
						layout.name,
						layout.fields[i].name);

		m_Layouts.push_back(std::move(layout));
		m_CppTypes.push_back(cppType);
		return static_cast<uint32_t>(m_Layouts.size() - 1);
	}

	const Layout*
	Schema::Find(std::string_view name) const noexcept
	{
		const auto it = std::ranges::find(m_Layouts, name, &Layout::name);
		return it == m_Layouts.end() ? nullptr : &*it;
	}

	std::optional<uint32_t>
	Schema::FindIndex(std::type_index cppType) const noexcept
	{
		if (cppType == std::type_index(typeid(void)))
			return std::nullopt;
		const auto it = std::ranges::find(m_CppTypes, cppType);
		return it == m_CppTypes.end() ?
		           std::nullopt :
		           std::optional(static_cast<uint32_t>(it - m_CppTypes.begin()));
	}

	const Layout&
	Schema::GetLayout(uint32_t index) const
	{
		if (index >= m_Layouts.size())
			throw_runtime_error("schema: no layout at index {}", index);
		return m_Layouts[index];
	}

	LayoutRef
	Schema::GetLayoutRef(std::string_view name) const
	{
		const auto it = std::ranges::find(m_Layouts, name, &Layout::name);
		if (it == m_Layouts.end())
			throw_runtime_error("schema: no layout named {}", name);
		return LayoutRef(*this, static_cast<uint32_t>(it - m_Layouts.begin()));
	}

	const Layout&
	LayoutRef::GetLayout() const
	{
		return m_Schema->GetLayout(m_Index);
	}

	size_t
	Schema::GetElementSize(const Field& field) const
	{
		return field.HoldsValues() ? valueSize(field.valueType) : GetLayout(field.layoutIndex).size;
	}

	size_t
	Schema::GetFieldSize(const Field& field) const
	{
		return GetElementSize(field) * field.count;
	}

	size_t
	Schema::GetFieldAlignment(const Field& field) const
	{
		return field.HoldsValues() ? valueSize(field.valueType) :
		                             GetLayoutAlignment(field.layoutIndex);
	}

	size_t
	Schema::GetLayoutAlignment(uint32_t index) const
	{
		size_t alignment = 1;
		for (const Field& field : GetLayout(index).fields)
			alignment = std::max(alignment, GetFieldAlignment(field));
		return alignment;
	}

	namespace
	{
		constexpr uint32_t c_TableVersion = 1;

		struct TableHeader
		{
			uint32_t version;
			uint32_t layoutCount;
			uint32_t fieldCount;
			uint32_t poolBytes;
		};

		static_assert(sizeof(TableHeader) == 16);

		struct LayoutRecord
		{
			uint32_t name;  // into the pool
			uint32_t size;
			uint32_t firstField;
			uint32_t fieldCount;
		};

		static_assert(sizeof(LayoutRecord) == 16);

		struct FieldRecord
		{
			uint32_t name;
			uint32_t offset;
			uint32_t count;
			uint32_t layoutIndex;
			uint8_t  type;
			uint8_t  valueType;
			uint8_t  pad[2];
		};

		static_assert(sizeof(FieldRecord) == 20);

		uint32_t
		intern(std::vector<char>& pool, std::string_view name)
		{
			const auto offset = static_cast<uint32_t>(pool.size());
			pool.insert(pool.end(), name.begin(), name.end());
			pool.push_back('\0');
			return offset;
		}

		std::string
		nameAt(std::span<const char> pool, uint32_t offset)
		{
			if (offset >= pool.size())
				throw_runtime_error("schema table: name offset {} is outside the pool", offset);
			const auto end = std::find(pool.begin() + offset, pool.end(), '\0');
			if (end == pool.end())
				throw_runtime_error("schema table: name at {} is not terminated", offset);
			return std::string(pool.begin() + offset, end);
		}
	}

	std::vector<std::byte>
	serialize(const Schema& schema)
	{
		std::vector<LayoutRecord> layouts;
		std::vector<FieldRecord>  fields;
		std::vector<char>         pool;
		layouts.reserve(schema.GetLayouts().size());

		for (const Layout& layout : schema.GetLayouts())
		{
			LayoutRecord record{};
			record.name       = intern(pool, layout.name);
			record.size       = layout.size;
			record.firstField = static_cast<uint32_t>(fields.size());
			record.fieldCount = static_cast<uint32_t>(layout.fields.size());
			layouts.push_back(record);

			for (const Field& field : layout.fields)
			{
				FieldRecord fieldRecord{};
				fieldRecord.name        = intern(pool, field.name);
				fieldRecord.offset      = field.offset;
				fieldRecord.count       = field.count;
				fieldRecord.layoutIndex = field.layoutIndex;
				fieldRecord.type        = static_cast<uint8_t>(field.type);
				fieldRecord.valueType   = static_cast<uint8_t>(field.valueType);
				fields.push_back(fieldRecord);
			}
		}

		TableHeader header{};
		header.version     = c_TableVersion;
		header.layoutCount = static_cast<uint32_t>(layouts.size());
		header.fieldCount  = static_cast<uint32_t>(fields.size());
		header.poolBytes   = static_cast<uint32_t>(pool.size());

		core::io::ByteWriter writer;
		writer.WritePod(header);
		writer.WritePodArray(std::span<const LayoutRecord>(layouts));
		writer.WritePodArray(std::span<const FieldRecord>(fields));
		writer.WriteBytes(std::as_bytes(std::span<const char>(pool)));
		return writer.Take();
	}

	Schema
	deserialize(std::span<const std::byte> bytes)
	{
		core::io::ByteReader reader(bytes);
		const auto           header = reader.ReadPod<TableHeader>();
		if (header.version != c_TableVersion)
			throw_runtime_error("schema table: unsupported version {}", header.version);

		std::vector<LayoutRecord> layouts(header.layoutCount);
		for (LayoutRecord& record : layouts) record = reader.ReadPod<LayoutRecord>();

		std::vector<FieldRecord> fields(header.fieldCount);
		for (FieldRecord& record : fields) record = reader.ReadPod<FieldRecord>();

		const auto poolBytes = reader.ReadBytes(header.poolBytes);
		const auto pool      = std::span<const char>(
			reinterpret_cast<const char*>(poolBytes.data()),
			poolBytes.size());

		Schema schema;
		for (const LayoutRecord& record : layouts)
		{
			if (record.firstField > fields.size() ||
			    record.fieldCount > fields.size() - record.firstField)
				throw_runtime_error("schema table: a layout's fields extend past the field table");

			Layout layout;
			layout.name = nameAt(pool, record.name);
			layout.size = record.size;
			layout.fields.reserve(record.fieldCount);
			for (uint32_t i = 0; i < record.fieldCount; ++i)
			{
				const FieldRecord& fieldRecord = fields[record.firstField + i];

				Field field;
				field.name        = nameAt(pool, fieldRecord.name);
				field.type        = static_cast<Type>(fieldRecord.type);
				field.valueType   = static_cast<ValueType>(fieldRecord.valueType);
				field.layoutIndex = fieldRecord.layoutIndex;
				field.offset      = fieldRecord.offset;
				field.count       = fieldRecord.count;
				layout.fields.push_back(std::move(field));
			}
			schema.Add(std::move(layout));
		}
		return schema;
	}
}
