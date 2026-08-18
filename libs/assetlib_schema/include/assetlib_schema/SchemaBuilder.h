#pragma once
#include <assetlib_schema/Schema.h>
#include <core/glm.h>
#include <core/type_traits.h>

namespace assetlib::schema
{
	/**
	 * What a member's C++ type is to the schema: a value, a struct, or a run of either. A glm
	 * vector is a run of floats, an array a run of its element, an enum its underlying integer --
	 * so a widened enum reads as a widened integer, which is what it is on disk.
	 */
	template <typename T>
	struct FieldTraits;

	template <typename T>
	concept SchemaValue = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

	template <SchemaValue T>
	struct FieldTraits<T>
	{
		static constexpr Type      c_Type = Type::kValue;
		static constexpr ValueType c_ValueType =
			std::is_floating_point_v<T> ? (sizeof(T) == 4 ? ValueType::kF32 : ValueType::kF64) :
			std::is_signed_v<T>         ? (sizeof(T) == 1 ? ValueType::kI8 :
		                                   sizeof(T) == 2 ? ValueType::kI16 :
		                                   sizeof(T) == 4 ? ValueType::kI32 :
		                                                    ValueType::kI64) :
										  (sizeof(T) == 1 ? ValueType::kU8 :
		                                   sizeof(T) == 2 ? ValueType::kU16 :
		                                   sizeof(T) == 4 ? ValueType::kU32 :
		                                                    ValueType::kU64);
		static constexpr uint32_t c_Count = 1;
		using Struct                      = void;
	};

	template <typename T>
		requires std::is_enum_v<T>
	struct FieldTraits<T> : FieldTraits<std::underlying_type_t<T>>
	{};

	template <glm::length_t L, typename T, glm::qualifier Q>
	struct FieldTraits<glm::vec<L, T, Q>>
	{
		static constexpr Type      c_Type      = Type::kArray;
		static constexpr ValueType c_ValueType = FieldTraits<T>::c_ValueType;
		static constexpr uint32_t  c_Count     = static_cast<uint32_t>(L);
		using Struct                           = void;
	};

	template <typename T, glm::qualifier Q>
	struct FieldTraits<glm::qua<T, Q>>
	{
		static constexpr Type      c_Type      = Type::kArray;
		static constexpr ValueType c_ValueType = FieldTraits<T>::c_ValueType;
		static constexpr uint32_t  c_Count     = 4;
		using Struct                           = void;
	};

	template <glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
	struct FieldTraits<glm::mat<C, R, T, Q>>
	{
		static constexpr Type      c_Type      = Type::kArray;
		static constexpr ValueType c_ValueType = FieldTraits<T>::c_ValueType;
		static constexpr uint32_t  c_Count     = static_cast<uint32_t>(C * R);
		using Struct                           = void;
	};

	template <typename T, size_t N>
	struct FieldTraits<std::array<T, N>>
	{
		static constexpr Type      c_Type      = Type::kArray;
		static constexpr ValueType c_ValueType = FieldTraits<T>::c_ValueType;
		static constexpr uint32_t  c_Count     = static_cast<uint32_t>(N) * FieldTraits<T>::c_Count;
		using Struct                           = typename FieldTraits<T>::Struct;
	};

	template <typename T, size_t N>
	struct FieldTraits<T[N]> : FieldTraits<std::array<T, N>>
	{};

	template <typename T>
		requires std::is_class_v<T>
	struct FieldTraits<T>
	{
		static constexpr Type      c_Type      = Type::kStruct;
		static constexpr ValueType c_ValueType = ValueType::kNone;
		static constexpr uint32_t  c_Count     = 1;
		using Struct                           = T;
	};

	/**
	 * Registers one POD's layout with a schema, a member pointer per field, so offset and type
	 * come from the compiler and cannot drift from the struct. Finish() adds the layout;
	 * Schema::Add is what refuses an incomplete one.
	 *
	 *     LayoutBuilder<Submesh>(schema, "Submesh")
	 *         .AddField("layout", &Submesh::layout)          // VertexLayout, registered earlier
	 *         .AddField("vertexByteOffset", &Submesh::vertexByteOffset)
	 *         .AddField("lodBias", &Submesh::lodBias).DefaultTo(0.0f)
	 *         .AddField("materialIndex", &Submesh::material).RenamedFrom("material")
	 *         .Finish();
	 */
	template <core::type_traits::trivially_copyable T>
	class LayoutBuilder
	{
	public:
		LayoutBuilder(Schema& schema, std::string_view name) : m_Schema(schema)
		{
			m_Layout.name = std::string(name);
			m_Layout.size = static_cast<uint32_t>(sizeof(T));
		}

		LayoutBuilder(const LayoutBuilder&) = delete;
		LayoutBuilder(LayoutBuilder&&)      = delete;
		LayoutBuilder&
		operator=(const LayoutBuilder&) = delete;
		LayoutBuilder&
		operator=(LayoutBuilder&&) = delete;

		/** @throws std::runtime_error if the member is a struct the schema has not registered. */
		template <typename M>
		LayoutBuilder&
		AddField(std::string_view name, M T::* member)
		{
			using Traits = FieldTraits<std::remove_cvref_t<M>>;

			Field field;
			field.name      = std::string(name);
			field.type      = Traits::c_Type;
			field.valueType = Traits::c_ValueType;
			field.offset    = OffsetOf(member);
			field.count     = Traits::c_Count;

			if constexpr (!std::is_void_v<typename Traits::Struct>)
			{
				const auto index = m_Schema.FindIndex(typeid(typename Traits::Struct));
				if (!index)
					throw std::runtime_error(
						"schema: " + m_Layout.name + "." + field.name +
						" is a struct the schema does not hold; register it first");
				field.layoutIndex = *index;
			}

			m_Layout.fields.push_back(std::move(field));
			return *this;
		}

		/** The name the last field may still carry in a file written before it was renamed. */
		LayoutBuilder&
		RenamedFrom(std::string_view oldName)
		{
			GetLast().formerly = std::string(oldName);
			return *this;
		}

		/**
		 * What the last field reads as from a file that does not carry it. The value is the whole
		 * field: a scalar for a scalar, an array for an array.
		 */
		template <core::type_traits::trivially_copyable V>
		LayoutBuilder&
		DefaultTo(const V& value)
		{
			auto& bytes = GetLast().defaultValue;
			bytes.resize(sizeof(V));
			std::copy_n(reinterpret_cast<const std::byte*>(&value), sizeof(V), bytes.data());
			return *this;
		}

		/** @return The index the schema gave the layout. @throws whatever Schema::Add throws. */
		uint32_t
		Finish()
		{
			return m_Schema.Add(std::move(m_Layout), typeid(T));
		}

	private:
		Field&
		GetLast()
		{
			if (m_Layout.fields.empty())
				throw std::runtime_error("schema: " + m_Layout.name + ": no field to qualify");
			return m_Layout.fields.back();
		}

		template <typename M>
		static uint32_t
		OffsetOf(M T::* member) noexcept
		{
			// Forms a member's address on storage that holds no object, as offsetof does; never
			// reads it. Unlike offsetof it takes a member pointer, so it needs no macro.
			alignas(T) std::byte storage[sizeof(T)];
			const T*             object = reinterpret_cast<const T*>(storage);
			return static_cast<uint32_t>(
				reinterpret_cast<const std::byte*>(&(object->*member)) - storage);
		}

		Schema& m_Schema;
		Layout  m_Layout;
	};
}
