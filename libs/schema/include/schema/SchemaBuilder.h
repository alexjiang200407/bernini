#pragma once
#include <schema/LayoutBuilder.h>
#include <schema/Schema.h>

namespace schema
{
	/**
	 * Assembles a Schema as one chain, a layout per call, and hands it over from Finish(). A
	 * builder with registrations of its own derives from this and returns itself from them; since
	 * AddLayout returns the base, such a chain registers its own layouts first and the private
	 * ones last -- which is also the order a struct's parts want.
	 *
	 *     const Schema schema = SchemaBuilder()
	 *         .AddLayout<Stamp>("Stamp", [](auto& l) { l.AddField("size", &Stamp::size); })
	 *         .Finish();
	 */
	class SchemaBuilder
	{
	public:
		/**
		 * One layout: `describe` declares its fields on a LayoutBuilder<T>, and this adds it.
		 * @throws whatever Schema::Add throws.
		 */
		template <
			core::type_traits::trivially_copyable T,
			std::invocable<LayoutBuilder<T>&>     Describe>
		SchemaBuilder&
		AddLayout(std::string_view name, Describe&& describe)
		{
			LayoutBuilder<T> layout(m_Schema, name);
			describe(layout);
			layout.Finish();
			return *this;
		}

		[[nodiscard]] Schema
		Finish()
		{
			return std::move(m_Schema);
		}

	protected:
		Schema m_Schema;
	};
}
