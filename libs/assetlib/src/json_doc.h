#pragma once
#include <nlohmann/json.hpp>

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
}
