#pragma once

namespace assetlib::schema
{
	/** What a field is: one value, one struct, or a run of either. */
	enum class Type : uint8_t
	{
		kValue,
		kStruct,
		kArray,
		kCount
	};

	/** The scalar a value field holds, or an array field's element. A glm::vec3 is three kF32. */
	enum class ValueType : uint8_t
	{
		kU8,
		kU16,
		kU32,
		kU64,
		kI8,
		kI16,
		kI32,
		kI64,
		kF32,
		kF64,
		kNone,
		kCount
	};

	/** @pre valueType is not kNone. */
	[[nodiscard]] size_t
	valueSize(ValueType valueType) noexcept;

	[[nodiscard]] std::string_view
	valueTypeName(ValueType valueType) noexcept;

	inline constexpr uint32_t c_NoLayout = 0xFFFFFFFFu;

	struct Field
	{
		std::string name;
		Type        type        = Type::kValue;
		ValueType   valueType   = ValueType::kNone;  // the value, or the array's value element
		uint32_t    layoutIndex = c_NoLayout;        // into Schema::GetLayouts: the struct, or the
													 // array's struct element
		uint32_t offset = 0;
		uint32_t count  = 1;  // kArray: elements; otherwise 1

		// Authoring side only -- neither reaches the byte form. A file's schema carries what it
		// stores; what to call it now and what to fill in when it is absent are the reader's.
		std::string            formerly;
		std::vector<std::byte> defaultValue;  // the whole field, count included; empty means zero

		/** kValue, or kArray of values. */
		[[nodiscard]] bool
		HoldsValues() const noexcept
		{
			return layoutIndex == c_NoLayout;
		}
	};

	/** One struct's layout: its fields by name, type, offset and count. */
	struct Layout
	{
		std::string        name;
		uint32_t           size = 0;
		std::vector<Field> fields;
	};

	class Schema;

	/**
	 * One layout and the schema it belongs to -- what a nested field's layoutIndex resolves within.
	 * A layout on its own cannot say what its struct fields are made of.
	 */
	class LayoutRef
	{
	public:
		LayoutRef(const Schema& schema, uint32_t index) noexcept : m_Schema(&schema), m_Index(index)
		{}

		[[nodiscard]] const Schema&
		GetSchema() const noexcept
		{
			return *m_Schema;
		}

		[[nodiscard]] uint32_t
		GetIndex() const noexcept
		{
			return m_Index;
		}

		[[nodiscard]] const Layout&
		GetLayout() const;

	private:
		const Schema* m_Schema;
		uint32_t      m_Index;
	};

	/**
	 * The layouts one container stores, in dependency order: a struct field refers to a layout at
	 * a lower index. Two schemas are compared by name, so the byte form carries names, types,
	 * offsets and counts and nothing that is only meaningful to the code that wrote it.
	 *
	 * A layout is accepted only if its fields tile its size: no two overlap, a gap before a field
	 * is smaller than that field's alignment, and the gap after the last is smaller than the
	 * layout's. That is what turns a field the builder forgot into an error at registration rather
	 * than a column of bytes no reader can name.
	 */
	class Schema
	{
	public:
		/**
		 * @param cppType The C++ type the layout describes, so a builder can resolve a nested
		 *                struct field to it; typeid(void) for a schema read back from bytes.
		 * @return The index the layout was given.
		 * @throws std::runtime_error if the layout is empty, reuses a name, refers to a layout this
		 *         schema does not hold, or its fields do not tile its size.
		 */
		uint32_t
		Add(Layout layout, std::type_index cppType = typeid(void));

		[[nodiscard]] const Layout*
		Find(std::string_view name) const noexcept;

		[[nodiscard]] std::optional<uint32_t>
		FindIndex(std::type_index cppType) const noexcept;

		[[nodiscard]] const Layout&
		GetLayout(uint32_t index) const;

		/** @throws std::runtime_error if no layout has that name. */
		[[nodiscard]] LayoutRef
		GetLayoutRef(std::string_view name) const;

		[[nodiscard]] std::span<const Layout>
		GetLayouts() const noexcept
		{
			return m_Layouts;
		}

		/** Bytes of one element of the field: its value's size, or its struct layout's. */
		[[nodiscard]] size_t
		GetElementSize(const Field& field) const;

		/** Bytes of the whole field, count included. */
		[[nodiscard]] size_t
		GetFieldSize(const Field& field) const;

		[[nodiscard]] size_t
		GetFieldAlignment(const Field& field) const;

		/** The largest alignment among the layout's fields, recursively. */
		[[nodiscard]] size_t
		GetLayoutAlignment(uint32_t index) const;

	private:
		std::vector<Layout>          m_Layouts;
		std::vector<std::type_index> m_CppTypes;
	};

	/**
	 * The schema as bytes: a fixed header, the layouts, the fields, a NUL-separated name pool.
	 * This byte layout is the one thing that can never change, because it is how a file says what
	 * its layout is.
	 */
	[[nodiscard]] std::vector<std::byte>
	serialize(const Schema& schema);

	/** @throws std::runtime_error on a truncated table, a bad version, or a layout Add would refuse. */
	[[nodiscard]] Schema
	deserialize(std::span<const std::byte> bytes);
}
