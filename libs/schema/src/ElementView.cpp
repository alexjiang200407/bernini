#include <schema/ElementView.h>

#include <core/err/util.h>

namespace schema
{
	using core::throw_runtime_error;

	ElementView::ElementView(LayoutRef layout, std::span<const std::byte> bytes) :
		m_Layout(layout), m_Bytes(bytes)
	{
		const Layout& description = m_Layout.GetLayout();
		if (bytes.size() % description.size != 0)
			throw_runtime_error(
				"{}: {} bytes is not a whole number of {}-byte elements",
				description.name,
				bytes.size(),
				description.size);
	}

	bool
	ElementView::Has(std::string_view field) const noexcept
	{
		return std::ranges::find(m_Layout.GetLayout().fields, field, &Field::name) !=
		       m_Layout.GetLayout().fields.end();
	}

	const Field&
	ElementView::Find(std::string_view field, size_t element) const
	{
		const Layout& description = m_Layout.GetLayout();
		if (element >= GetCount())
			throw_runtime_error("{}: element {} of {}", description.name, element, GetCount());
		const auto it = std::ranges::find(description.fields, field, &Field::name);
		if (it == description.fields.end())
			throw_runtime_error("{}: no field named {}", description.name, field);
		return *it;
	}

	std::vector<std::byte>
	ElementView::ReadValues(
		std::string_view field,
		size_t           element,
		ValueType        wanted,
		uint32_t         wantedCount) const
	{
		const Field&  stored      = Find(field, element);
		const Layout& description = m_Layout.GetLayout();
		if (!stored.HoldsValues())
			throw_runtime_error(
				"{}.{}: holds {}, read as a value",
				description.name,
				field,
				fieldShape(m_Layout.GetSchema(), stored));
		if (stored.count != wantedCount || !widens(stored.valueType, wanted))
			throw_runtime_error(
				"{}.{}: file stores {}, read as {}{}",
				description.name,
				field,
				fieldShape(m_Layout.GetSchema(), stored),
				valueTypeName(wanted),
				wantedCount == 1 ? std::string() : "[" + std::to_string(wantedCount) + "]");

		const auto bytes = m_Bytes.subspan(
			element * description.size + stored.offset,
			m_Layout.GetSchema().GetFieldSize(stored));
		return convertValues(stored.valueType, bytes, wanted);
	}

	ElementView
	ElementView::GetStruct(std::string_view field, size_t element) const
	{
		const Field&  stored      = Find(field, element);
		const Layout& description = m_Layout.GetLayout();
		if (stored.HoldsValues())
			throw_runtime_error(
				"{}.{}: holds {}, read as a struct",
				description.name,
				field,
				fieldShape(m_Layout.GetSchema(), stored));
		return ElementView(
			LayoutRef(m_Layout.GetSchema(), stored.layoutIndex),
			m_Bytes.subspan(
				element * description.size + stored.offset,
				m_Layout.GetSchema().GetFieldSize(stored)));
	}
}
