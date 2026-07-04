#pragma once

namespace core
{
	/**
	 * A read-only view of a single settings value, returned by Settings::operator[]
	 * and freely chainable: indexing or converting an absent / null value never
	 * throws.
	 *
	 *
	 * Converting a null (or type-mismatched) accessor yields the target type's
	 * default (0, 0.0, false, ""); GetOrDefault supplies an explicit fallback.
	 *
	 * An accessor points into the data owned by its parent Settings and must not
	 * outlive it.
	 */
	class SettingsAccessor
	{
	public:
		[[nodiscard]] SettingsAccessor
		operator[](const char* key) const noexcept;

		[[nodiscard]] SettingsAccessor
		operator[](std::string_view key) const noexcept;

		operator bool() const noexcept;
		operator int() const noexcept;
		operator unsigned int() const noexcept;
		operator int64_t() const noexcept;
		operator uint64_t() const noexcept;
		operator float() const noexcept;
		operator std::string() const;

		[[nodiscard]] bool
		GetOrDefault(bool defaultValue) const noexcept;

		[[nodiscard]] int
		GetOrDefault(int defaultValue) const noexcept;

		[[nodiscard]] unsigned int
		GetOrDefault(unsigned int defaultValue) const noexcept;

		[[nodiscard]] int64_t
		GetOrDefault(int64_t defaultValue) const noexcept;

		[[nodiscard]] uint64_t
		GetOrDefault(uint64_t defaultValue) const noexcept;

		[[nodiscard]] float
		GetOrDefault(float defaultValue) const noexcept;

		[[nodiscard]] std::string
		GetOrDefault(std::string defaultValue) const;

		/// True when this accessor refers to nothing (absent key or JSON null).
		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Value == nullptr;
		}

	private:
		friend class Settings;

		explicit SettingsAccessor(const void* value) noexcept : m_Value(value) {}
		const void* m_Value = nullptr;
	};

	/**
	 * Immutable settings loaded once from a JSON file. The values cannot be changed
	 * after construction; look them up with operator[], which returns a chainable
	 * SettingsAccessor.
	 *
	 *     core::Settings settings("config.json");
	 *     int msaa = settings["graphics"]["msaa"].GetOrDefault(1);
	 */
	class Settings
	{
	public:
		/**
		 * Loads and parses the JSON file at `filepath`.
		 * @throws std::runtime_error if the file cannot be opened or parsed.
		 */
		explicit Settings(const std::filesystem::path& filepath);

		~Settings();

		Settings(const Settings&) = delete;
		Settings&
		operator=(const Settings&) = delete;

		Settings(Settings&&) noexcept;
		Settings&
		operator=(Settings&&) noexcept;

		[[nodiscard]] SettingsAccessor
		operator[](std::string_view key) const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};
}
