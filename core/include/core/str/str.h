namespace core::str
{
	std::wstring
	string_to_wide(std::string_view str);

	std::string
	wide_to_string(std::wstring_view ws);

	size_t toUtf32(std::span<char16_t>, std::span<char32_t>);
}
