namespace core::str
{
	std::wstring
	string_to_wide(std::string_view str);

	std::string
	wide_to_string(std::wstring_view ws);
}
