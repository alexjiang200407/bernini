#pragma once
#include <algorithm>
#include <concepts>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

// The shape every authored text document shares: parsed strictly, written canonically -- sorted
// keys, tab indent, one trailing newline -- so one document is one byte sequence and two
// checkouts that agree on the content agree on the file.
namespace assetlib::doc
{
	/** @throws std::runtime_error unless `text` is a JSON object; `what` prefixes the message. */
	[[nodiscard]] nlohmann::json
	parseObject(std::string_view text, std::string_view what);

	[[nodiscard]] std::string
	canonicalDump(const nlohmann::json& json);

	/**
	 * `value` as the double whose shortest decimal is the *float's* shortest decimal -- so a
	 * hand-typed `0.6` stays `0.6` on every save instead of growing seventeen digits, in a format
	 * whose purpose is diffing and merging. Parsing it back to float is exact either way.
	 */
	[[nodiscard]] double
	plainFloat(float value);

	/** The canonical dump, as the bytes a container writer stores. */
	[[nodiscard]] std::vector<std::byte>
	toBytes(const nlohmann::json& json);

	template <typename Vec>
	nlohmann::json
	vecToJson(const Vec& value)
	{
		constexpr size_t c_N = sizeof(Vec) / sizeof(float);

		auto array = nlohmann::json::array();
		for (size_t i = 0; i < c_N; ++i)
			array.push_back(plainFloat(value[static_cast<glm::length_t>(i)]));
		return array;
	}

	template <typename Vec>
	void
	vecFromJson(const nlohmann::json& json, std::string_view key, Vec& out, std::string_view what)
	{
		constexpr size_t c_N = sizeof(Vec) / sizeof(float);

		core::throw_runtime_error_if(
			!json.is_array() || json.size() != c_N ||
				!std::ranges::all_of(json, [](const auto& v) { return v.is_number(); }),
			"{}: '{}' is not an array of {} numbers",
			what,
			key,
			c_N);
		for (size_t i = 0; i < c_N; ++i)
			out[static_cast<glm::length_t>(i)] = json[i].template get<float>();
	}

	/** The value shapes a document's known keys use -- what `take` can pull out. */
	template <typename T>
	concept DocumentValue =
		std::same_as<T, std::string> || std::same_as<T, float> || std::same_as<T, uint32_t> ||
		std::same_as<T, bool> || std::same_as<T, glm::vec3> || std::same_as<T, glm::vec4>;

	/** Takes `key` out of `json` into `out` when present, so what remains is the unknown. */
	template <DocumentValue T>
	void
	take(nlohmann::json& json, std::string_view key, T& out, std::string_view what);

	/**
	 * `take` bound to one object and one message prefix, so a reader of many keys names them
	 * alone. Holds references; outlive it with both.
	 */
	class Taker
	{
	public:
		Taker(nlohmann::json& json, std::string_view what) noexcept : m_Json(json), m_What(what) {}

		Taker(const Taker&) = delete;
		Taker(Taker&&)      = delete;
		Taker&
		operator=(const Taker&) = delete;
		Taker&
		operator=(Taker&&) = delete;

		template <DocumentValue T>
		void
		Take(std::string_view key, T& out) const
		{
			take(m_Json, key, out, m_What);
		}

	private:
		nlohmann::json&  m_Json;
		std::string_view m_What;
	};

	template <DocumentValue T>
	void
	take(nlohmann::json& json, std::string_view key, T& out, std::string_view what)
	{
		const auto it = json.find(key);
		if (it == json.end())
			return;

		if constexpr (std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
			vecFromJson(*it, key, out, what);
		else if constexpr (std::is_same_v<T, float>)
		{
			core::throw_runtime_error_if(!it->is_number(), "{}: '{}' is not a number", what, key);
			out = it->get<float>();
		}
		else if constexpr (std::is_same_v<T, uint32_t>)
		{
			core::throw_runtime_error_if(
				!it->is_number_unsigned() || it->get<uint64_t>() > UINT32_MAX,
				"{}: '{}' is not a 32-bit unsigned number",
				what,
				key);
			out = it->get<uint32_t>();
		}
		else if constexpr (std::is_same_v<T, bool>)
		{
			core::throw_runtime_error_if(!it->is_boolean(), "{}: '{}' is not a boolean", what, key);
			out = it->get<bool>();
		}
		else
		{
			core::throw_runtime_error_if(!it->is_string(), "{}: '{}' is not a string", what, key);
			out = it->get<std::string>();
		}
		json.erase(it);
	}
}
