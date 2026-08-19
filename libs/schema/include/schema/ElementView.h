#pragma once
#include <core/type_traits.h>
#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>
#include <schema/convert.h>

namespace schema
{
	/**
	 * A run of elements as a file holds them, read by field name through the file's own layout.
	 * What a hook looks through: the generic conversion has already produced the current struct,
	 * and a hook that has to recover a value the new shape does not carry reads the old one here.
	 */
	class ElementView
	{
	public:
		/** @throws std::runtime_error if the bytes are not a whole number of the layout's elements. */
		ElementView(LayoutRef layout, std::span<const std::byte> bytes);

		[[nodiscard]] size_t
		GetCount() const noexcept
		{
			return m_Bytes.size() / m_Layout.GetLayout().size;
		}

		[[nodiscard]] bool
		Has(std::string_view field) const noexcept;

		/**
		 * The value of a field of `element`, as a T the stored value widens to. A glm::vec3 or a
		 * std::array reads a whole array field of the same count.
		 *
		 * @throws std::runtime_error if there is no such field, it holds structs, its shape is not
		 *         T's, or `element` is past the end.
		 */
		template <core::type_traits::trivially_copyable T>
			requires std::is_void_v<typename FieldTraits<std::remove_cvref_t<T>>::Struct>
		[[nodiscard]] T
		Get(std::string_view field, size_t element = 0) const
		{
			using Traits = FieldTraits<std::remove_cvref_t<T>>;

			const auto bytes = ReadValues(field, element, Traits::c_ValueType, Traits::c_Count);
			assert(bytes.size() == sizeof(T));
			T value;
			std::copy_n(bytes.data(), sizeof(T), reinterpret_cast<std::byte*>(&value));
			return value;
		}

		/**
		 * A struct field of `element`, or the run of them for an array of structs, as its own view.
		 * @throws std::runtime_error if there is no such field, it holds values, or `element` is
		 *         past the end.
		 */
		[[nodiscard]] ElementView
		GetStruct(std::string_view field, size_t element = 0) const;

	private:
		[[nodiscard]] const Field&
		Find(std::string_view field, size_t element) const;

		[[nodiscard]] std::vector<std::byte>
		ReadValues(std::string_view field, size_t element, ValueType wanted, uint32_t wantedCount)
			const;

		LayoutRef                  m_Layout;
		std::span<const std::byte> m_Bytes;
	};
}
