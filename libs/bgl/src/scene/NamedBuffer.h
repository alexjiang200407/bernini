#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
namespace bgl
{
	/** One of an owner's buffers, paired with the frame-graph name it is imported under. */
	template <typename Owner, typename Buffer>
	struct NamedBuffer
	{
		std::string_view name;
		Buffer Owner::* member;
	};

	template <typename Owner, typename Buffer>
	NamedBuffer(std::string_view, Buffer Owner::*) -> NamedBuffer<Owner, Buffer>;

	/** Calls `fn(name, buffer)` for each entry of `table`, in declaration order. */
	template <typename Owner, typename... Entries, typename Fn>
	constexpr void
	ForEachNamedBuffer(Owner& owner, const std::tuple<Entries...>& table, Fn&& fn)
	{
		std::apply(
			[&](const auto&... entry) { (..., fn(entry.name, owner.*entry.member)); },
			table);
	}

	/**
	 * Whether a table of NamedBuffers names each of them differently.
	 *
	 * A repeated name is not a diagnosed collision: the second import overwrites the first, and
	 * every pass reading that name is handed the wrong buffer.
	 */
	template <typename... Entries>
	consteval bool
	HasDistinctNames(const std::tuple<Entries...>& table) noexcept
	{
		const auto names =
			std::apply([](const auto&... entry) { return std::array{ entry.name... }; }, table);

		for (size_t i = 0; i < names.size(); ++i)
		{
			for (size_t j = i + 1; j < names.size(); ++j)
			{
				if (names[i] == names[j])
				{
					return false;
				}
			}
		}
		return true;
	}
}
