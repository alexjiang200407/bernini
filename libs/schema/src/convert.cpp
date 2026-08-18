#include <schema/convert.h>

#include <core/err/util.h>

namespace schema
{
	using core::throw_runtime_error;

	namespace
	{
		bool
		isFloat(ValueType type) noexcept
		{
			return type == ValueType::kF32 || type == ValueType::kF64;
		}

		bool
		isSigned(ValueType type) noexcept
		{
			return type == ValueType::kI8 || type == ValueType::kI16 || type == ValueType::kI32 ||
			       type == ValueType::kI64;
		}

		template <typename T>
		T
		readAs(const std::byte* src) noexcept
		{
			T value;
			std::copy_n(src, sizeof(T), reinterpret_cast<std::byte*>(&value));
			return value;
		}

		template <typename T>
		void
		writeAs(std::byte* dst, T value) noexcept
		{
			std::copy_n(reinterpret_cast<const std::byte*>(&value), sizeof(T), dst);
		}

		int64_t
		readSigned(ValueType type, const std::byte* src) noexcept
		{
			switch (valueSize(type))
			{
			case 1:
				return readAs<int8_t>(src);
			case 2:
				return readAs<int16_t>(src);
			case 4:
				return readAs<int32_t>(src);
			default:
				return readAs<int64_t>(src);
			}
		}

		uint64_t
		readUnsigned(ValueType type, const std::byte* src) noexcept
		{
			switch (valueSize(type))
			{
			case 1:
				return readAs<uint8_t>(src);
			case 2:
				return readAs<uint16_t>(src);
			case 4:
				return readAs<uint32_t>(src);
			default:
				return readAs<uint64_t>(src);
			}
		}

		void
		writeSigned(ValueType type, std::byte* dst, int64_t value) noexcept
		{
			switch (valueSize(type))
			{
			case 1:
				writeAs(dst, static_cast<int8_t>(value));
				break;
			case 2:
				writeAs(dst, static_cast<int16_t>(value));
				break;
			case 4:
				writeAs(dst, static_cast<int32_t>(value));
				break;
			default:
				writeAs(dst, value);
				break;
			}
		}

		void
		writeUnsigned(ValueType type, std::byte* dst, uint64_t value) noexcept
		{
			switch (valueSize(type))
			{
			case 1:
				writeAs(dst, static_cast<uint8_t>(value));
				break;
			case 2:
				writeAs(dst, static_cast<uint16_t>(value));
				break;
			case 4:
				writeAs(dst, static_cast<uint32_t>(value));
				break;
			default:
				writeAs(dst, value);
				break;
			}
		}

		/** @pre widens(stored, wanted). */
		void
		widenValue(
			ValueType        stored,
			const std::byte* src,
			ValueType        wanted,
			std::byte*       dst) noexcept
		{
			if (isFloat(stored))
			{
				const double value =
					stored == ValueType::kF32 ? readAs<float>(src) : readAs<double>(src);
				if (wanted == ValueType::kF32)
					writeAs(dst, static_cast<float>(value));
				else
					writeAs(dst, value);
			}
			else if (isSigned(stored))
			{
				writeSigned(wanted, dst, readSigned(stored, src));
			}
			else
			{
				const uint64_t value = readUnsigned(stored, src);
				if (isSigned(wanted))
					writeSigned(wanted, dst, static_cast<int64_t>(value));
				else
					writeUnsigned(wanted, dst, value);
			}
		}

		const Field*
		findStored(const Layout& stored, const Field& wanted) noexcept
		{
			const auto byName = [&](std::string_view name) -> const Field* {
				const auto it = std::ranges::find(stored.fields, name, &Field::name);
				return it == stored.fields.end() ? nullptr : &*it;
			};
			if (const Field* field = byName(wanted.name))
				return field;
			return wanted.formerly.empty() ? nullptr : byName(wanted.formerly);
		}

		[[noreturn]] void
		throwNoConversion(
			const Schema& stored,
			const Field&  storedField,
			const Schema& wanted,
			const Field&  wantedField,
			const Layout& wantedLayout)
		{
			throw_runtime_error(
				"{}.{}: file stores {}, engine wants {}, no conversion",
				wantedLayout.name,
				wantedField.name,
				fieldShape(stored, storedField),
				fieldShape(wanted, wantedField));
		}

		/** The default's bytes for elements [first, count), or zero where there is no default. */
		void
		writeDefaultTail(const Schema& wanted, const Field& field, std::byte* dst, uint32_t first)
		{
			if (field.defaultValue.empty())
				return;
			const auto elementSize = wanted.GetElementSize(field);
			std::copy(
				field.defaultValue.begin() + static_cast<ptrdiff_t>(first * elementSize),
				field.defaultValue.end(),
				dst + first * elementSize);
		}

		void
		convertElement(
			const Schema&    stored,
			const Layout&    storedLayout,
			const std::byte* src,
			const Schema&    wanted,
			const Layout&    wantedLayout,
			std::byte*       dst);

		void
		convertField(
			const Schema&    stored,
			const Field&     storedField,
			const std::byte* src,
			const Schema&    wanted,
			const Field&     wantedField,
			std::byte*       dst,
			const Layout&    wantedLayout)
		{
			if (storedField.HoldsValues() != wantedField.HoldsValues())
				throwNoConversion(stored, storedField, wanted, wantedField, wantedLayout);

			const uint32_t shared = std::min(storedField.count, wantedField.count);

			if (storedField.HoldsValues())
			{
				if (storedField.valueType == wantedField.valueType)
				{
					std::copy_n(src, shared * valueSize(storedField.valueType), dst);
				}
				else if (widens(storedField.valueType, wantedField.valueType))
				{
					const auto storedSize = valueSize(storedField.valueType);
					const auto wantedSize = valueSize(wantedField.valueType);
					for (uint32_t i = 0; i < shared; ++i)
						widenValue(
							storedField.valueType,
							src + i * storedSize,
							wantedField.valueType,
							dst + i * wantedSize);
				}
				else
				{
					throwNoConversion(stored, storedField, wanted, wantedField, wantedLayout);
				}
			}
			else
			{
				const Layout& storedElement = stored.GetLayout(storedField.layoutIndex);
				const Layout& wantedElement = wanted.GetLayout(wantedField.layoutIndex);
				for (uint32_t i = 0; i < shared; ++i)
					convertElement(
						stored,
						storedElement,
						src + i * storedElement.size,
						wanted,
						wantedElement,
						dst + i * wantedElement.size);
			}

			if (wantedField.count > shared)
				writeDefaultTail(wanted, wantedField, dst, shared);
		}

		void
		convertElement(
			const Schema&    stored,
			const Layout&    storedLayout,
			const std::byte* src,
			const Schema&    wanted,
			const Layout&    wantedLayout,
			std::byte*       dst)
		{
			for (const Field& wantedField : wantedLayout.fields)
			{
				std::byte* out = dst + wantedField.offset;
				if (const Field* storedField = findStored(storedLayout, wantedField))
					convertField(
						stored,
						*storedField,
						src + storedField->offset,
						wanted,
						wantedField,
						out,
						wantedLayout);
				else
					writeDefaultTail(wanted, wantedField, out, 0);
			}
		}
	}

	bool
	widens(ValueType stored, ValueType wanted) noexcept
	{
		if (stored == ValueType::kNone || wanted == ValueType::kNone)
			return false;
		if (stored == wanted)
			return true;
		if (isFloat(stored) || isFloat(wanted))
			return stored == ValueType::kF32 && wanted == ValueType::kF64;

		const auto storedSize = valueSize(stored);
		const auto wantedSize = valueSize(wanted);
		if (isSigned(stored) && !isSigned(wanted))
			return false;
		return wantedSize > storedSize;
	}

	std::vector<std::byte>
	convertValues(ValueType stored, std::span<const std::byte> storedBytes, ValueType wanted)
	{
		if (!widens(stored, wanted))
			throw_runtime_error(
				"file stores {}, engine wants {}, no conversion",
				valueTypeName(stored),
				valueTypeName(wanted));

		const auto storedSize = valueSize(stored);
		if (storedBytes.size() % storedSize != 0)
			throw_runtime_error(
				"{} bytes is not a whole number of {} values",
				storedBytes.size(),
				valueTypeName(stored));

		if (stored == wanted)
			return std::vector<std::byte>(storedBytes.begin(), storedBytes.end());

		const auto             count      = storedBytes.size() / storedSize;
		const auto             wantedSize = valueSize(wanted);
		std::vector<std::byte> out(count * wantedSize);
		for (size_t i = 0; i < count; ++i)
			widenValue(
				stored,
				storedBytes.data() + i * storedSize,
				wanted,
				out.data() + i * wantedSize);
		return out;
	}

	std::vector<std::byte>
	convert(LayoutRef stored, std::span<const std::byte> storedBytes, LayoutRef wanted)
	{
		const Layout& storedLayout = stored.GetLayout();
		const Layout& wantedLayout = wanted.GetLayout();

		if (storedBytes.size() % storedLayout.size != 0)
			throw_runtime_error(
				"{}: {} bytes is not a whole number of {}-byte elements",
				storedLayout.name,
				storedBytes.size(),
				storedLayout.size);

		const auto             count = storedBytes.size() / storedLayout.size;
		std::vector<std::byte> out(count * wantedLayout.size);
		for (size_t i = 0; i < count; ++i)
			convertElement(
				stored.GetSchema(),
				storedLayout,
				storedBytes.data() + i * storedLayout.size,
				wanted.GetSchema(),
				wantedLayout,
				out.data() + i * wantedLayout.size);
		return out;
	}

	bool
	sameLayout(LayoutRef a, LayoutRef b)
	{
		const Layout& left  = a.GetLayout();
		const Layout& right = b.GetLayout();
		if (left.size != right.size || left.fields.size() != right.fields.size())
			return false;
		for (size_t i = 0; i < left.fields.size(); ++i)
		{
			const Field& l = left.fields[i];
			const Field& r = right.fields[i];
			if (l.name != r.name || l.type != r.type || l.valueType != r.valueType ||
			    l.offset != r.offset || l.count != r.count || l.HoldsValues() != r.HoldsValues())
				return false;
			if (!l.HoldsValues() && !sameLayout(
										LayoutRef(a.GetSchema(), l.layoutIndex),
										LayoutRef(b.GetSchema(), r.layoutIndex)))
				return false;
		}
		return true;
	}

	std::string
	fieldShape(const Schema& schema, const Field& field)
	{
		std::string text = field.HoldsValues() ?
		                       std::string(valueTypeName(field.valueType)) :
		                       "struct " + schema.GetLayout(field.layoutIndex).name;
		if (field.type == Type::kArray)
			text += "[" + std::to_string(field.count) + "]";
		return text;
	}
}
