#include <editor_plugin_api/translation_csv.h>

#include "translation_validation.h"

#include <QByteArray>
#include <QString>
#include <cstddef>
#include <editor_plugin_api/TranslationCatalog.h>
#include <qtypes.h>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	std::vector<std::vector<std::string>>
	ReadRows(std::string_view csv)
	{
		if (csv.starts_with("\xEF\xBB\xBF"))
			csv.remove_prefix(3);
		const auto decoded = QString::fromUtf8(csv.data(), static_cast<qsizetype>(csv.size()));
		const auto encoded = decoded.toUtf8();
		if (std::string_view(encoded.constData(), static_cast<std::size_t>(encoded.size())) !=
		        csv ||
		    csv.find('\0') != std::string_view::npos)
			throw std::runtime_error("Translation CSV must be valid UTF-8 without NUL bytes");

		std::vector<std::vector<std::string>> rows;
		std::size_t                           position = 0;
		while (position < csv.size())
		{
			std::vector<std::string> row;
			while (true)
			{
				std::string field;
				if (position < csv.size() && csv[position] == '"')
				{
					++position;
					bool closed = false;
					while (position < csv.size())
					{
						const auto c = csv[position++];
						// A newline inside a quoted field is the message's own, and a catalog a
						// spreadsheet saved spells it CRLF. A lone CR is content and stays one.
						if (c == '\r' && position < csv.size() && csv[position] == '\n')
							continue;
						if (c != '"')
							field += c;
						else if (position < csv.size() && csv[position] == '"')
						{
							field += '"';
							++position;
						}
						else
						{
							closed = true;
							break;
						}
					}
					if (!closed)
						throw std::runtime_error("Unclosed translation CSV quote");
				}
				else
				{
					while (position < csv.size() && csv[position] != ',' && csv[position] != '\r' &&
					       csv[position] != '\n')
					{
						if (csv[position] == '"')
							throw std::runtime_error("Quote inside unquoted translation CSV field");
						field += csv[position++];
					}
				}
				row.push_back(std::move(field));
				if (position == csv.size())
					break;
				const auto separator = csv[position++];
				if (separator == ',')
					continue;
				if (separator == '\n')
					break;
				if (separator == '\r' && position < csv.size() && csv[position] == '\n')
				{
					++position;
					break;
				}
				throw std::runtime_error("Invalid translation CSV field separator");
			}
			rows.push_back(std::move(row));
		}
		return rows;
	}
}

namespace editor
{
	TranslationCatalog
	ReadTranslationCsv(std::string_view context, std::string_view csv)
	{
		detail::ValidateContext(context);
		const auto rows = ReadRows(csv);
		if (rows.empty() || rows.front().size() < 2 || rows.front().front() != "key")
			throw std::runtime_error("Translation CSV requires key and locale columns");
		const auto&           header = rows.front();
		std::set<std::string> locales;
		for (std::size_t column = 1; column < header.size(); ++column)
		{
			detail::ValidateLocale(header[column]);
			if (!locales.insert(header[column]).second)
				throw std::runtime_error("Duplicate translation CSV locale");
		}
		TranslationCatalog    catalog{ std::string(context), {} };
		std::set<std::string> keys;
		for (std::size_t index = 1; index < rows.size(); ++index)
		{
			const auto& row = rows[index];
			if (row.size() != header.size() || !detail::IsKey(row.front()))
				throw std::runtime_error("Invalid translation CSV row or key");
			if (!keys.insert(row.front()).second)
				throw std::runtime_error("Duplicate translation CSV key");
			for (std::size_t column = 1; column < row.size(); ++column)
			{
				if (!row[column].empty())
					catalog.entries.push_back(
						{ row.front(),
					      header[column],
					      QString::fromUtf8(
							  row[column].data(),
							  static_cast<qsizetype>(row[column].size())) });
			}
		}
		return catalog;
	}
}
