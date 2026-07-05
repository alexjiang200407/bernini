#include <core/settings/Settings.h>
#include <nlohmann/json.hpp>

namespace core
{
	struct Settings::Impl
	{
		nlohmann::json root;
	};

	namespace
	{
		// Pulls any JSON numeric / boolean value out of `node` as T, converting
		// across number kinds (a value stored as unsigned still reads back as int,
		// float, etc.). Returns `defaultValue` for null, missing, or non-numeric
		// nodes (strings, objects, arrays).
		template <typename T>
		T
		asNumber(const void* node, T defaultValue) noexcept
		{
			if (node == nullptr)
			{
				return defaultValue;
			}

			const auto& json = *static_cast<const nlohmann::json*>(node);

			if (json.is_number_unsigned())
			{
				return static_cast<T>(json.get<uint64_t>());
			}
			if (json.is_number_integer())
			{
				return static_cast<T>(json.get<int64_t>());
			}
			if (json.is_number_float())
			{
				return static_cast<T>(json.get<double>());
			}
			if (json.is_boolean())
			{
				return static_cast<T>(json.get<bool>());
			}

			return defaultValue;
		}

		std::string
		asString(const void* node, std::string defaultValue)
		{
			if (node == nullptr)
			{
				return defaultValue;
			}

			const auto& json = *static_cast<const nlohmann::json*>(node);
			if (json.is_string())
			{
				return json.get<std::string>();
			}

			return defaultValue;
		}
	}

	SettingsAccessor
	SettingsAccessor::operator[](const char* key) const noexcept
	{
		return (*this)[key != nullptr ? std::string_view(key) : std::string_view()];
	}

	SettingsAccessor
	SettingsAccessor::operator[](std::string_view key) const noexcept
	{
		if (m_Value == nullptr)
		{
			return SettingsAccessor(nullptr);
		}

		const auto& json = *static_cast<const nlohmann::json*>(m_Value);
		if (!json.is_object())
		{
			return SettingsAccessor(nullptr);
		}

		const auto it = json.find(std::string(key));
		if (it == json.end())
		{
			return SettingsAccessor(nullptr);
		}

		return SettingsAccessor(&(*it));
	}

	SettingsAccessor::
	operator bool() const noexcept
	{
		return asNumber<bool>(m_Value, false);
	}

	SettingsAccessor::
	operator int() const noexcept
	{
		return asNumber<int>(m_Value, 0);
	}

	SettingsAccessor::
	operator unsigned int() const noexcept
	{
		return asNumber<unsigned int>(m_Value, 0u);
	}

	SettingsAccessor::
	operator int64_t() const noexcept
	{
		return asNumber<int64_t>(m_Value, 0);
	}

	SettingsAccessor::
	operator uint64_t() const noexcept
	{
		return asNumber<uint64_t>(m_Value, 0);
	}

	SettingsAccessor::
	operator float() const noexcept
	{
		return asNumber<float>(m_Value, 0.0f);
	}

	SettingsAccessor::
	operator std::string() const
	{
		return asString(m_Value, std::string());
	}

	bool
	SettingsAccessor::GetOrDefault(bool defaultValue) const noexcept
	{
		return asNumber<bool>(m_Value, defaultValue);
	}

	int
	SettingsAccessor::GetOrDefault(int defaultValue) const noexcept
	{
		return asNumber<int>(m_Value, defaultValue);
	}

	unsigned int
	SettingsAccessor::GetOrDefault(unsigned int defaultValue) const noexcept
	{
		return asNumber<unsigned int>(m_Value, defaultValue);
	}

	int64_t
	SettingsAccessor::GetOrDefault(int64_t defaultValue) const noexcept
	{
		return asNumber<int64_t>(m_Value, defaultValue);
	}

	uint64_t
	SettingsAccessor::GetOrDefault(uint64_t defaultValue) const noexcept
	{
		return asNumber<uint64_t>(m_Value, defaultValue);
	}

	float
	SettingsAccessor::GetOrDefault(float defaultValue) const noexcept
	{
		return asNumber<float>(m_Value, defaultValue);
	}

	std::string
	SettingsAccessor::GetOrDefault(std::string defaultValue) const
	{
		return asString(m_Value, std::move(defaultValue));
	}

	Settings::Settings(const std::filesystem::path& filepath) : m_Impl(std::make_unique<Impl>())
	{
		std::ifstream file(filepath);
		if (!file.is_open())
		{
			throw std::runtime_error(
				std::format("Settings: failed to open '{}'", filepath.string()));
		}

		try
		{
			m_Impl->root = nlohmann::json::parse(file);
		}
		catch (const nlohmann::json::parse_error& e)
		{
			throw std::runtime_error(
				std::format("Settings: failed to parse '{}': {}", filepath.string(), e.what()));
		}
	}

	Settings::~Settings() = default;

	Settings::Settings(Settings&&) noexcept = default;

	Settings&
	Settings::operator=(Settings&&) noexcept = default;

	SettingsAccessor
	Settings::operator[](std::string_view key) const noexcept
	{
		const void* root = m_Impl != nullptr ? &m_Impl->root : nullptr;
		return SettingsAccessor(root)[key];
	}
}
