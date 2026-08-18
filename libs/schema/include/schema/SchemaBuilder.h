#pragma once
#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>

namespace schema
{
	/**
	 * Assembles a Schema as one chain, a layout per call, and hands it over from Finish(). The
	 * chain keeps the derived type -- `Derived` is the class deriving from this -- so a builder
	 * that adds named registrations of its own (`AddTransform()`, `AddNode()`) chains them with
	 * AddLayout in any order.
	 *
	 *     const Schema schema = SchemaBuilder()
	 *         .AddLayout<Stamp>("Stamp", [](auto& l) { l.AddField("size", &Stamp::size); })
	 *         .Finish();
	 */
	template <typename Derived>
	class SchemaBuilderBase
	{
	public:
		/**
		 * One layout: `describe` declares its fields on a LayoutBuilder<T>, and this adds it.
		 * @throws whatever Schema::Add throws.
		 */
		template <
			core::type_traits::trivially_copyable T,
			std::invocable<LayoutBuilder<T>&>     Describe>
		Derived&
		AddLayout(std::string_view name, Describe&& describe)
			requires std::derived_from<Derived, SchemaBuilderBase<Derived>>
		{
			LayoutBuilder<T> layout(m_Schema, name);
			describe(layout);
			layout.Finish();
			return static_cast<Derived&>(*this);
		}

		[[nodiscard]] Schema
		Finish()
		{
			return std::move(m_Schema);
		}

	protected:
		Schema m_Schema;
	};

	class SchemaBuilder final : public SchemaBuilderBase<SchemaBuilder>
	{};
}
