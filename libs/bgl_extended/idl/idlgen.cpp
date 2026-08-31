#include <CLI/CLI.hpp>
#include <core/err/util.h>
#include <core/math.h>
#include <slang-com-ptr.h>
#include <slang.h>

/**
 * bgl_idlgen - generate C++ POD structs, enums, and constants (and a
 * banner-stamped Slang copy) from an `.slang` IDL file, keeping the CPU and GPU
 * definitions in sync.
 *
 * Usage:
 *   bgl_idlgen --src-root <dir> [--cpp-out-dir <dir>] [--slang-out-dir <dir>]
 *              [--namespace ns] [--metal-layout] [--public] [-I <search-dir>]... <input.slang>
 *
 * At least one of --cpp-out-dir / --slang-out-dir must be given.
 */

using Slang::ComPtr;
namespace fs = std::filesystem;

namespace
{
	// ---- type mapping --------------------------------------------------------

	using ScalarType = slang::TypeReflection::ScalarType;
	using TypeKind   = slang::TypeReflection::Kind;

	std::string
	ScalarToCpp(ScalarType s)
	{
		switch (s)
		{
		case ScalarType::Bool:
			return "bool";
		case ScalarType::Int8:
			return "int8_t";
		case ScalarType::UInt8:
			return "uint8_t";
		case ScalarType::Int16:
			return "int16_t";
		case ScalarType::UInt16:
			return "uint16_t";
		case ScalarType::Int32:
			return "int32_t";
		case ScalarType::UInt32:
			return "uint32_t";
		case ScalarType::Int64:
			return "int64_t";
		case ScalarType::UInt64:
			return "uint64_t";
		case ScalarType::Float32:
			return "float";
		case ScalarType::Float64:
			return "double";
		default:
			core::throw_runtime_error("unsupported scalar type {}", (int)s);
		}
	}

	std::string
	VectorPrefix(ScalarType elem)
	{
		switch (elem)
		{
		case ScalarType::Float32:
			return "glm::vec";
		case ScalarType::Float64:
			return "glm::dvec";
		case ScalarType::Int32:
			return "glm::ivec";
		case ScalarType::UInt32:
			return "glm::uvec";
		case ScalarType::Bool:
			return "glm::bvec";
		default:
			core::throw_runtime_error("unsupported vector element type {}", (int)elem);
		}
	}

	std::string
	VectorToCpp(ScalarType elem, int n)
	{
		return std::format("{}{}", VectorPrefix(elem), n);
	}

	std::string
	MatrixToCpp(ScalarType elem, int rows, int cols)
	{
		if (elem != ScalarType::Float32)
		{
			throw std::runtime_error("only float matrices are supported");
		}

		// glm is column-major; glm::matCxR. Square matrices collapse to glm::matN.
		if (rows == cols)
		{
			return std::format("glm::mat{}", rows);
		}
		return std::format("glm::mat{}x{}", cols, rows);
	}

	std::string
	StripName(std::string name)
	{
		if (auto lt = name.find('<'); lt != std::string::npos)
		{
			name = name.substr(0, lt);
		}
		if (auto qual = name.rfind("::"); qual != std::string::npos)
		{
			name = name.substr(qual + 2);
		}
		return name;
	}

	std::string
	CppBaseType(slang::TypeReflection* t, std::set<std::string>& referenced)
	{
		switch (t->getKind())
		{
		case TypeKind::Scalar:
			return ScalarToCpp(t->getScalarType());
		case TypeKind::Vector:
			return VectorToCpp(t->getElementType()->getScalarType(), (int)t->getElementCount());
		case TypeKind::Matrix:
			return MatrixToCpp(t->getScalarType(), t->getRowCount(), t->getColumnCount());
		case TypeKind::Struct:
		case TypeKind::Enum:
		{
			std::string name = StripName(t->getName());
			referenced.insert(name);
			return name;
		}
		default:
			core::throw_runtime_error("unsupported field type kind {}", (int)t->getKind());
		}
	}

	struct FieldInfo
	{
		std::string type;  // C++ base type, no array suffix
		std::string name;
		std::string arraySuffix;  // e.g. "[8]" for a fixed array, else empty
		size_t      offset;
	};

	struct StructInfo
	{
		std::string name;
		size_t      size;
		// 0 when the C/C++ rules already give the target's alignment, so no alignas is emitted.
		size_t                 alignment = 0;
		std::vector<FieldInfo> fields;
	};

	struct EnumInfo
	{
		std::string name;
		std::string underlying = "uint32_t";
		// The underlying type as written in the source (`enum Foo : uint16_t`), empty when the
		// declaration omits it. When set it overrides the reflected/defaulted size + underlying.
		std::string declaredUnderlying;
		size_t      size = 4;
		// (enumerator, valueText). valueText is either a decimal (from an int literal or the running
		// counter, preserving explicit-numeric output) or a verbatim non-integer initializer
		// expression (e.g. "uint8_t(-1)"); empty means emit the enumerator bare (C++ auto-increments).
		std::vector<std::pair<std::string, std::string>> enumerators;
	};

	struct ConstantInfo
	{
		std::string type;  // C++ type, or "auto" for an inferred (`let`) constant
		std::string name;
		std::string value;  // RHS expression, copied verbatim
	};

	// Map a Slang scalar type keyword (as written in the IDL source) to its C++
	// spelling. `let` (an inferred type) becomes `auto`. Anything unrecognised is
	// passed through unchanged so exact C++ type names (e.g. uint32_t) still work.
	std::string
	ConstTypeToCpp(const std::string& slangType)
	{
		static const std::map<std::string, std::string> c_Map = {
			{ "let", "auto" },    { "int", "int32_t" },   { "uint", "uint32_t" },
			{ "float", "float" }, { "double", "double" }, { "bool", "bool" },
		};
		if (auto it = c_Map.find(slangType); it != c_Map.end())
		{
			return it->second;
		}
		return slangType;
	}

	std::string
	UnderlyingForSize(size_t size)
	{
		switch (size)
		{
		case 1:
			return "uint8_t";
		case 2:
			return "uint16_t";
		case 8:
			return "uint64_t";
		case 4:
		default:
			return "uint32_t";
		}
	}

	// Byte size of an explicitly-declared enum underlying type (`enum Foo : uintN_t`).
	size_t
	SizeForUnderlying(const std::string& u)
	{
		if (u == "uint8_t" || u == "int8_t" || u == "bool")
			return 1;
		if (u == "uint16_t" || u == "int16_t")
			return 2;
		if (u == "uint64_t" || u == "int64_t")
			return 8;
		return 4;  // uint / int / uint32_t / int32_t / unknown
	}

	void
	CollectStructDecls(slang::DeclReflection* decl, std::vector<slang::DeclReflection*>& out)
	{
		for (slang::DeclReflection* child : decl->getChildren())
		{
			switch (child->getKind())
			{
			case slang::DeclReflection::Kind::Struct:
				out.push_back(child);
				break;
			case slang::DeclReflection::Kind::Namespace:
				CollectStructDecls(child, out);
				break;
			default:
				break;
			}
		}
	}

	// A struct needs the GPU's ScalarDataLayout (not the host layout) exactly when it embeds a
	// bindless descriptor handle: a `.Handle` is a pointer on the host (8-aligned) but a uint2 on
	// the GPU (4-aligned), so the two layouts disagree on struct size and array stride. Those
	// structs are precisely the ones stored in ScalarDataLayout EntryBuffers. Everything else keeps
	// the host layout, which already matches its buffer and the C/C++ mirror.
	bool
	ContainsHandle(slang::TypeReflection* type)
	{
		if (type == nullptr || type->getKind() != TypeKind::Struct)
		{
			return false;
		}
		for (unsigned i = 0; i < type->getFieldCount(); ++i)
		{
			slang::TypeReflection* ftype = type->getFieldByIndex(i)->getType();
			while (ftype->getKind() == TypeKind::Array)
			{
				ftype = ftype->getElementType();
			}
			if (StripName(ftype->getName()) == "DescriptorHandle" || ContainsHandle(ftype))
			{
				return true;
			}
		}
		return false;
	}

	// What MSL makes of a type. Computed rather than reflected: Slang's Metal reflection reports a
	// resource handle as zero ordinary-data bytes -- its categories count bindings, not bytes -- so
	// TextureHandle comes back as 0 instead of 8. Asking would under-report
	// every struct carrying a handle, which is every material. The backend recomputes constant-buffer
	// layouts for the same reason (see MetalizeLayout); these are MSL's rules for buffer elements.
	struct MslLayout
	{
		size_t size  = 0;
		size_t align = 1;
	};

	size_t
	MslScalarSize(slang::TypeReflection::ScalarType scalar)
	{
		using ST = slang::TypeReflection::ScalarType;
		switch (scalar)
		{
		case ST::Bool:
		case ST::Int8:
		case ST::UInt8:
			return 1;
		case ST::Int16:
		case ST::UInt16:
		case ST::Float16:
			return 2;
		case ST::Int32:
		case ST::UInt32:
		case ST::Float32:
			return 4;
		case ST::Int64:
		case ST::UInt64:
		case ST::Float64:
			return 8;
		default:
			std::cerr << "error: no MSL size known for scalar type " << int(scalar) << "\n";
			std::exit(1);
		}
	}

	// `metalVectors` picks the rule set: MSL aligns a vector or matrix column to its own width,
	// where glm leaves it at its scalar's. Both walks give a handle 8 bytes and 8 alignment, because
	// DescriptorHandle carries alignas(8) on a Metal build -- so the C++ walk describes what the
	// compiler will actually emit there, which the host reflection cannot know.
	MslLayout
	LayoutOf(slang::TypeReflection* type, bool metalVectors)
	{
		switch (type->getKind())
		{
		case TypeKind::Scalar:
		{
			const size_t size = MslScalarSize(type->getScalarType());
			return { size, size };
		}
		case TypeKind::Vector:
		{
			// MSL aligns a vector to its own width, and rounds a 3-component one up to 4.
			const size_t elem  = MslScalarSize(type->getElementType()->getScalarType());
			const size_t lanes = type->getElementCount() == 3 ? 4 : type->getElementCount();
			return { lanes * elem, metalVectors ? lanes * elem : elem };
		}
		case TypeKind::Matrix:
		{
			// Column-major, one padded vector per column.
			const size_t elem  = MslScalarSize(type->getScalarType());
			const size_t lanes = type->getRowCount() == 3 ? 4 : type->getRowCount();
			return { type->getColumnCount() * lanes * elem, metalVectors ? lanes * elem : elem };
		}
		case TypeKind::Array:
		{
			const MslLayout elem = LayoutOf(type->getElementType(), metalVectors);
			return { elem.size * type->getElementCount(), elem.align };
		}
		case TypeKind::Enum:
			return LayoutOf(type->getElementType(), metalVectors);
		case TypeKind::Struct:
		{
			// A bindless handle is a device pointer or an MTLResourceID: 8 bytes, 8-aligned,
			// whatever it points at.
			if (StripName(type->getName()) == "DescriptorHandle")
			{
				return { 8, 8 };
			}

			MslLayout out;
			for (unsigned i = 0; i < type->getFieldCount(); ++i)
			{
				const MslLayout field = LayoutOf(type->getFieldByIndex(i)->getType(), metalVectors);
				out.align             = std::max(out.align, field.align);
				out.size              = core::round_up(out.size, field.align) + field.size;
			}
			out.size = core::round_up(out.size, out.align);
			return out;
		}
		default:
			std::cerr << std::format(
				"error: no MSL layout known for type kind {}\n",
				int(type->getKind()));
			std::exit(1);
		}
	}

	// Where each member of `type` lands, in declaration order, under the chosen rules.
	std::vector<size_t>
	FieldOffsets(slang::TypeReflection* type, bool metalVectors)
	{
		std::vector<size_t> offsets;
		size_t              cursor = 0;
		for (unsigned i = 0; i < type->getFieldCount(); ++i)
		{
			const MslLayout field = LayoutOf(type->getFieldByIndex(i)->getType(), metalVectors);
			cursor                = core::round_up(cursor, field.align);
			offsets.push_back(cursor);
			cursor += field.size;
		}
		return offsets;
	}

	StructInfo
	ReflectStruct(
		slang::DeclReflection*         decl,
		slang::ProgramLayout*          layout,
		slang::ProgramLayout*          scalarLayout,
		std::set<std::string>&         referenced,
		std::map<std::string, size_t>& enumSizes,
		bool                           metalLayout,
		bool                           isPublic)
	{
		slang::TypeReflection* type = decl->getType();

		// A handle-bearing struct is laid out by its ScalarDataLayout EntryBuffer element -- that is
		// how the GPU reads it and it is byte-compatible with the emitted C/C++ mirror. The default
		// rules the host target would give instead 8-align the handle, disagreeing with the mirror.
		slang::TypeLayoutReflection* tlayout;
		if (ContainsHandle(type))
		{
			const std::string sbName =
				std::format("StructuredBuffer<{}, ScalarDataLayout>", type->getName());
			slang::TypeReflection* sbType = scalarLayout->findTypeByName(sbName.c_str());
			tlayout =
				sbType ? scalarLayout->getTypeLayout(sbType)->getElementTypeLayout() : nullptr;
			if (tlayout == nullptr)
			{
				std::cerr << std::format(
					"error: failed to reflect scalar layout of '{}'\n",
					type->getName());
				std::exit(1);
			}
		}
		else
		{
			tlayout = layout->getTypeLayout(type);
		}

		StructInfo info;
		info.name = StripName(type->getName());
		// Stride, not size: the C++ mirror's sizeof rounds up to the struct's alignment (as does an
		// array element / buffer stride), whereas getSize() is the unpadded tail. They differ for a
		// struct whose last member is smaller than its alignment (e.g. a uint16 after a handle).
		info.size = tlayout->getStride();

		// MSL diverges from the C/C++ scalar rules in two ways that matter here: it aligns a
		// resource handle to 8, and it rounds a struct up to its own alignment. Where that lands a
		// *member* somewhere else, no alignment can reconcile the two and the struct has to be
		// written differently.
		// Two walks: what MSL does, and what the C++ compiler will do on a Metal build (where
		// DescriptorHandle is alignas(8)). The host reflection describes neither -- it lays handles
		// out 4-aligned, which is the D3D12 truth only.
		const MslLayout           msl        = LayoutOf(type, /*metalVectors*/ true);
		const MslLayout           cpp        = LayoutOf(type, /*metalVectors*/ false);
		const std::vector<size_t> mslOffsets = FieldOffsets(type, /*metalVectors*/ true);
		const std::vector<size_t> cppOffsets = FieldOffsets(type, /*metalVectors*/ false);

		// A struct alignment closes a gap at the end. Where MSL puts a *member* somewhere the C++
		// rules will not, nothing closes it and the struct has to be written differently.
		for (size_t i = 0; i < mslOffsets.size() && i < cppOffsets.size(); ++i)
		{
			if (mslOffsets[i] == cppOffsets[i])
			{
				continue;
			}
			std::cerr << std::format(
				"error: '{}::{}' sits at {} under MSL but {} under the C++ rules; no struct "
				"alignment can fix an interior mismatch -- reorder the struct so its widest "
				"members come first\n",
				type->getName(),
				tlayout->getFieldByIndex(static_cast<unsigned>(i))->getName(),
				mslOffsets[i],
				cppOffsets[i]);
			std::exit(1);
		}

		// A public header is committed and every backend compiles against that one copy, so it may
		// only hold a struct that lays out identically on all of them. Nothing else would report
		// this: a generated header's static_asserts come from the same layout as its struct, so
		// they agree with themselves whichever target produced them.
		if (isPublic && msl.size != info.size)
		{
			std::cerr << std::format(
				"error: '{}' is {} bytes under MSL and {} under the C/C++ rules, so it cannot go "
				"in a public header -- those are committed and shared by every backend. Move it to "
				"IDL_CPP_SOURCES, or write it so both agree.\n",
				type->getName(),
				msl.size,
				info.size);
			std::exit(1);
		}

		if (metalLayout)
		{
			info.size = msl.size;
			// Only where the target wants more than the members give on their own; otherwise sizeof
			// already agrees and an alignas would be noise.
			if (msl.align > cpp.align)
			{
				info.alignment = msl.align;
			}
		}

		const unsigned fieldCount = tlayout->getFieldCount();
		for (unsigned i = 0; i < fieldCount; ++i)
		{
			slang::VariableLayoutReflection* var = tlayout->getFieldByIndex(i);

			// Use the *declared* field type for the C++ type name. The host layout
			// lowers an enum field to its underlying scalar, which would erase the
			// enum name (emitting `int32_t` instead of `VertexSemantic`); the
			// declared type off the VariableReflection keeps it. Offsets/sizes
			// still come from the layout below.
			slang::TypeReflection* ftype = var->getTypeLayout()->getType();
			if (slang::VariableReflection* varDecl = var->getVariable())
			{
				if (slang::TypeReflection* declared = varDecl->getType())
				{
					ftype = declared;
				}
			}

			std::string arraySuffix;
			while (ftype->getKind() == TypeKind::Array)
			{
				arraySuffix += std::format("[{}]", ftype->getElementCount());
				ftype = ftype->getElementType();
			}

			if (ftype->getKind() == TypeKind::Enum)
			{
				if (slang::TypeLayoutReflection* etl = layout->getTypeLayout(ftype))
				{
					enumSizes[StripName(ftype->getName())] = etl->getSize();
				}
			}

			// A 3-component vector is 12 bytes to the C/C++ mirror and 16 to MSL, so every member
			// after one sits at a different offset on Metal -- an interior mismatch no trailing
			// padding can close. Refused here rather than left for a shader to read wrong, because
			// the author of the next struct has no reason to know it.
			if (ftype->getKind() == TypeKind::Vector && ftype->getElementCount() == 3)
			{
				std::cerr << std::format(
					"error: '{}::{}' is a 3-component vector, which the C/C++ rules size at 12 "
					"bytes and MSL at 16; use a 4-component one (a bounding sphere packs as "
					"xyz + w) so both agree\n",
					info.name,
					var->getName());
				std::exit(1);
			}

			FieldInfo field;
			field.name        = var->getName();
			field.type        = CppBaseType(ftype, referenced);
			field.arraySuffix = arraySuffix;
			// Under --metal-layout the host reflection is the wrong ruler: it lays a handle out
			// 4-aligned, where the mirror this header describes has it at 8.
			field.offset = metalLayout && i < mslOffsets.size() ? mslOffsets[i] : var->getOffset();
			info.fields.push_back(std::move(field));
		}

		return info;
	}

	std::map<std::string, std::string>
	ParseImports(const std::string& source)
	{
		std::map<std::string, std::string> bySegment;
		std::istringstream                 in(source);
		std::string                        line;
		while (std::getline(in, line))
		{
			auto pos = line.find("import");
			if (pos == std::string::npos)
			{
				continue;
			}
			pos += 6;
			while (pos < line.size() && std::isspace((unsigned char)line[pos]))
			{
				++pos;
			}
			std::string module;
			while (pos < line.size() &&
			       (std::isalnum((unsigned char)line[pos]) || line[pos] == '.' || line[pos] == '_'))
			{
				module += line[pos++];
			}
			if (module.empty())
			{
				continue;
			}

			std::string header = module;
			std::replace(header.begin(), header.end(), '.', '/');
			header += ".h";

			std::string segment = module;
			if (auto dot = segment.rfind('.'); dot != std::string::npos)
			{
				segment = segment.substr(dot + 1);
			}
			bySegment[segment] = header;
		}
		return bySegment;
	}

	bool
	IsIdentChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
	}

	std::string
	StripLineComments(const std::string& src)
	{
		std::string        out;
		std::istringstream in(src);
		std::string        line;
		while (std::getline(in, line))
		{
			if (auto c = line.find("//"); c != std::string::npos)
			{
				line = line.substr(0, c);
			}
			out += line;
			out += '\n';
		}
		return out;
	}

	/**
	 * Textually parse `enum` declarations (name + enumerators) from an IDL
	 * source. Slang's DeclReflection does not reliably surface enum cases, and
	 * the module source is the single source of truth we already copy verbatim,
	 * so a small deterministic parser is preferable to fragile reflection here.
	 * Enumerator values default to a running counter unless `= <int>` is given.
	 * The underlying size is filled in later from struct-field reflection.
	 */
	std::vector<EnumInfo>
	ParseEnums(const std::string& rawSource)
	{
		const std::string     source = StripLineComments(rawSource);
		std::vector<EnumInfo> enums;

		size_t search = 0;
		while (true)
		{
			size_t kw = source.find("enum", search);
			if (kw == std::string::npos)
			{
				break;
			}
			search = kw + 4;

			// Whole-word check so "enumCount" or a mid-identifier match is ignored.
			const bool leftOk  = (kw == 0) || !IsIdentChar(source[kw - 1]);
			const bool rightOk = (search >= source.size()) || !IsIdentChar(source[search]);
			if (!leftOk || !rightOk)
			{
				continue;
			}

			size_t p = search;
			while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])))
			{
				++p;
			}

			std::string name;
			while (p < source.size() && IsIdentChar(source[p]))
			{
				name += source[p++];
			}
			if (name.empty())
			{
				continue;
			}

			// Optional explicit underlying type: `enum Name : uintN_t { ... }`.
			std::string declaredUnderlying;
			{
				size_t q = p;
				while (q < source.size() && std::isspace(static_cast<unsigned char>(source[q])))
				{
					++q;
				}
				if (q < source.size() && source[q] == ':')
				{
					++q;
					while (q < source.size() && std::isspace(static_cast<unsigned char>(source[q])))
					{
						++q;
					}
					while (q < source.size() && IsIdentChar(source[q]))
					{
						declaredUnderlying += source[q++];
					}
					p = q;
				}
			}

			const size_t brace = source.find('{', p);
			if (brace == std::string::npos)
			{
				break;
			}
			const size_t close = source.find('}', brace);
			if (close == std::string::npos)
			{
				break;
			}

			EnumInfo info;
			info.name               = name;
			info.declaredUnderlying = declaredUnderlying;

			const std::string body         = source.substr(brace + 1, close - brace - 1);
			long long         nextValue    = 0;
			bool              counterValid = true;  // false after a non-integer initializer
			size_t            itemStart    = 0;
			while (itemStart <= body.size())
			{
				const size_t comma = body.find(',', itemStart);
				const size_t len =
					(comma == std::string::npos) ? body.size() - itemStart : comma - itemStart;
				std::string item = body.substr(itemStart, len);

				const size_t first = item.find_first_not_of(" \t\r\n");
				const size_t last  = item.find_last_not_of(" \t\r\n");
				if (first != std::string::npos)
				{
					item = item.substr(first, last - first + 1);

					std::string enumeratorName = item;
					// valueText: decimal for an int literal / running counter, verbatim for a
					// non-integer initializer, or empty to emit the enumerator bare.
					std::string valueText;
					if (const size_t eq = item.find('='); eq != std::string::npos)
					{
						enumeratorName = item.substr(0, eq);
						if (const size_t ne = enumeratorName.find_last_not_of(" \t");
						    ne != std::string::npos)
						{
							enumeratorName = enumeratorName.substr(0, ne + 1);
						}

						std::string  rhs = item.substr(eq + 1);
						const size_t rf  = rhs.find_first_not_of(" \t\r\n");
						const size_t rl  = rhs.find_last_not_of(" \t\r\n");
						if (rf != std::string::npos)
						{
							rhs = rhs.substr(rf, rl - rf + 1);
						}

						// Treat as an integer only if the whole RHS is one integer literal;
						// otherwise copy it verbatim (e.g. `uint8_t(-1)`).
						long long parsed = 0;
						size_t    idx    = 0;
						bool      isInt  = false;
						try
						{
							parsed = std::stoll(rhs, &idx, 0);
							isInt  = (idx == rhs.size());
						}
						catch (const std::exception&)
						{
							isInt = false;
						}

						if (isInt)
						{
							valueText    = std::to_string(parsed);
							nextValue    = parsed + 1;
							counterValid = true;
						}
						else
						{
							valueText    = rhs;  // verbatim
							counterValid = false;
						}
					}
					else if (counterValid)
					{
						valueText = std::to_string(nextValue);
						++nextValue;
					}
					// else: leave valueText empty -> emit bare, let C++ auto-increment.

					if (!enumeratorName.empty())
					{
						info.enumerators.emplace_back(enumeratorName, valueText);
					}
				}

				if (comma == std::string::npos)
				{
					break;
				}
				itemStart = comma + 1;
			}

			enums.push_back(std::move(info));
			search = close + 1;
		}

		return enums;
	}

	/**
	 * Textually parse module-scope `static const` declarations from an IDL source.
	 * As with enum values, a constant's initializer is not reliably reflected, and
	 * the source is already the single source of truth we copy verbatim, so a small
	 * deterministic parser is used. Recognises:
	 *
	 *     [public] static const <type|let> <name> = <expr>;
	 *
	 * capturing the name and the RHS expression verbatim. The RHS is emitted into
	 * the C++ header unchanged, so it must be valid in both languages (integer and
	 * float literals, arithmetic on them, etc.). A leading `public` (needed for the
	 * constant to be visible to importing shaders) is skipped implicitly.
	 */
	std::vector<ConstantInfo>
	ParseConstants(const std::string& rawSource)
	{
		const std::string         source = StripLineComments(rawSource);
		std::vector<ConstantInfo> constants;

		auto skipSpace = [&](size_t& p) {
			while (p < source.size() && std::isspace(static_cast<unsigned char>(source[p])))
			{
				++p;
			}
		};
		auto readIdent = [&](size_t& p) {
			std::string id;
			while (p < source.size() && IsIdentChar(source[p]))
			{
				id += source[p++];
			}
			return id;
		};

		size_t search = 0;
		while (true)
		{
			size_t kw = source.find("static", search);
			if (kw == std::string::npos)
			{
				break;
			}
			search = kw + 6;

			// Whole-word check so a mid-identifier match is ignored.
			const bool leftOk  = (kw == 0) || !IsIdentChar(source[kw - 1]);
			const bool rightOk = (search >= source.size()) || !IsIdentChar(source[search]);
			if (!leftOk || !rightOk)
			{
				continue;
			}

			size_t p = search;
			skipSpace(p);
			if (readIdent(p) != "const")  // only `static const` module constants
			{
				continue;
			}
			skipSpace(p);
			const std::string type = readIdent(p);
			skipSpace(p);
			const std::string name = readIdent(p);
			skipSpace(p);
			if (type.empty() || name.empty() || p >= source.size() || source[p] != '=')
			{
				continue;
			}
			++p;

			const size_t semi = source.find(';', p);
			if (semi == std::string::npos)
			{
				break;
			}

			std::string  value = source.substr(p, semi - p);
			const size_t first = value.find_first_not_of(" \t\r\n");
			const size_t last  = value.find_last_not_of(" \t\r\n");
			search             = semi + 1;
			if (first == std::string::npos)
			{
				continue;
			}
			value = value.substr(first, last - first + 1);

			ConstantInfo info;
			info.type  = ConstTypeToCpp(type);
			info.name  = name;
			info.value = value;
			constants.push_back(std::move(info));
		}

		return constants;
	}

	std::string
	Banner(const std::string& from)
	{
		return std::format("// THIS IS A FILE GENERATED FROM {}. DO NOT EDIT MANUALLY\n", from);
	}

	std::string
	EmitCpp(
		const std::string&                        bannerFrom,
		const std::string&                        ns,
		const std::vector<ConstantInfo>&          constants,
		const std::vector<EnumInfo>&              enums,
		const std::vector<StructInfo>&            structs,
		const std::set<std::string>&              referenced,
		const std::map<std::string, std::string>& imports)
	{
		std::string out;
		out += Banner(bannerFrom);
		out += "#pragma once\n";

		std::set<std::string> includes;
		for (const std::string& name : referenced)
		{
			if (auto it = imports.find(name); it != imports.end())
			{
				includes.insert(it->second);
			}
		}
		for (const std::string& header : includes)
		{
			out += std::format("#include \"{}\"\n", header);
		}

		out += std::format("\nnamespace {}\n{{\n", ns);

		// Enums first: constants below may cast an enumerator (e.g. `uint16_t(PsoType::kCount)`)
		// and struct fields reference enums by name; a locally defined enum emits no #include.
		for (const EnumInfo& e : enums)
		{
			out += std::format("\tenum class {} : {}\n\t{{\n", e.name, e.underlying);
			for (const auto& [enumerator, value] : e.enumerators)
			{
				if (value.empty())
				{
					out += std::format("\t\t{},\n", enumerator);
				}
				else
				{
					out += std::format("\t\t{} = {},\n", enumerator, value);
				}
			}
			out += "\t};\n\n";
			out += std::format("\tstatic_assert(sizeof({}) == {});\n\n", e.name, e.size);
		}

		// Constants after enums (so they can reference an enumerator) but before structs (which
		// may use a constant as an array bound).
		for (const ConstantInfo& c : constants)
		{
			// A float literal needs its suffix: without one the initializer is a double, which MSVC
			// reports as a narrowing truncation and this project builds warnings as errors.
			const bool suffixed =
				!c.value.empty() && (c.value.back() == 'f' || c.value.back() == 'F');
			const std::string value = (c.type == "float" && !suffixed) ? c.value + "f" : c.value;

			out += std::format("\tconstexpr {} {} = {};\n", c.type, c.name, value);
		}
		if (!constants.empty())
		{
			out += "\n";
		}

		for (const StructInfo& s : structs)
		{
			// alignas rather than a trailing pad member: it is what the target actually requires,
			// and sizeof follows from it without inventing a field the shader would have to skip.
			if (s.alignment > 0)
			{
				out += std::format("\tstruct alignas({}) {}\n\t{{\n", s.alignment, s.name);
			}
			else
			{
				out += std::format("\tstruct {}\n\t{{\n", s.name);
			}
			for (const FieldInfo& f : s.fields)
			{
				out += std::format("\t\t{} {}{};\n", f.type, f.name, f.arraySuffix);
			}
			out += "\t};\n\n";

			out += std::format("\tstatic_assert(sizeof({}) == {});\n", s.name, s.size);
			for (const FieldInfo& f : s.fields)
			{
				out += std::format(
					"\tstatic_assert(offsetof({}, {}) == {});\n",
					s.name,
					f.name,
					f.offset);
			}
			out += "\n";
		}

		out += "}\n";
		return out;
	}

	std::string
	ReadFile(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			core::throw_runtime_error("could not open input file {}", path.string());
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	/**
	 * Writes `content` with LF line endings, and only when that is not already what is on disk.
	 *
	 */
	void
	WriteFile(const fs::path& path, std::string content)
	{
		std::erase(content, '\r');

		std::error_code ec;
		if (fs::exists(path, ec) && ReadFile(path) == content)
		{
			return;
		}

		if (path.has_parent_path())
		{
			fs::create_directories(path.parent_path());
		}
		std::ofstream out(path, std::ios::binary);
		if (!out)
		{
			core::throw_runtime_error("could not open output file {}", path.string());
		}
		out << content;
	}

	void
	ReportDiagnostics(slang::IBlob* diagnostics)
	{
		if (diagnostics && diagnostics->getBufferSize() > 0)
		{
			std::cerr << (const char*)diagnostics->getBufferPointer() << "\n";
		}
	}
}

int
main(int argc, char** argv)
{
	CLI::App app{ "Generate C++ structs from a Slang IDL file via reflection" };

	std::string              input;
	std::string              srcRoot;
	std::string              cppOutDir;
	std::string              slangOutDir;
	std::string              baseNs = "bgl::idl";
	std::vector<std::string> includeDirs;
	bool                     metalLayout = false;
	bool                     isPublic    = false;

	app.add_option("input", input, "Input .slang IDL file")->required();
	app.add_option("--src-root", srcRoot, "Root the input's module path is relative to")
		->required();
	app.add_option("--cpp-out-dir", cppOutDir, "Output root for the generated C++ header");
	app.add_option("--slang-out-dir", slangOutDir, "Output root for the banner-stamped Slang copy");
	app.add_option("--namespace", baseNs, "Base C++ namespace for the generated structs")
		->capture_default_str();
	app.add_option("-I,--include", includeDirs, "Search directory for imported Slang modules");
	app.add_flag(
		"--metal-layout",
		metalLayout,
		"Lay the C++ structs out by MSL's rules rather than the C/C++ scalar ones");
	app.add_flag(
		"--public",
		isPublic,
		"This header is committed and shared by every backend, so refuse a struct whose layout "
		"is not the same on all of them");

	CLI11_PARSE(app, argc, argv);

	if (cppOutDir.empty() && slangOutDir.empty())
	{
		std::cerr << "error: at least one of --cpp-out-dir / --slang-out-dir is required\n";
		return 1;
	}

	try
	{
		const fs::path inputPath = fs::absolute(input);
		const fs::path rootPath  = fs::absolute(srcRoot);

		const fs::path rel = fs::relative(inputPath, rootPath);
		if (rel.empty() || *rel.begin() == "..")
		{
			core::throw_runtime_error("input {} is not under --src-root {}", input, srcRoot);
		}

		fs::path relNoExt = rel;
		relNoExt.replace_extension();

		const std::string moduleLoadName = relNoExt.generic_string();

		std::string ns = baseNs;
		for (const fs::path& part : relNoExt.parent_path())
		{
			ns += "::" + part.string();
		}

		const std::string source = ReadFile(inputPath);

		// Reported to Slang as this module's own path so that a sibling `import` resolves next to the
		// *generated* copies -- Slang looks beside the importing file before it consults the search
		// paths, and the sources there are unpadded.
		const fs::path selfPath =
			slangOutDir.empty() ? inputPath : fs::absolute(fs::path(slangOutDir) / rel);

		// A module with no C++ mirror is interface- or generic-only, so it has no concrete layout to
		// pad and the copy goes out verbatim. Every other module's copy is written after reflection,
		// which is what knows the padding.
		if (cppOutDir.empty())
		{
			if (!slangOutDir.empty())
			{
				const fs::path slangOut = fs::path(slangOutDir) / rel;
				WriteFile(slangOut, Banner(rel.generic_string()) + "\n" + source);
			}
			return 0;
		}

		const fs::path cppOut = fs::path(cppOutDir) / relNoExt.concat(".h");

		ComPtr<slang::IGlobalSession> globalSession;
		if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef())))
		{
			std::cerr << "error: failed to create Slang global session\n";
			return 1;
		}

		slang::TargetDesc target{};
		target.format = SLANG_HOST_HOST_CALLABLE;  // C/C++ (scalar) layout rules

		std::vector<std::string> searchPaths;
		// The generated copies come first, so an import resolves to the same module text the shaders
		// see. This module itself is loaded from its source text, never from its own generated copy.
		if (!slangOutDir.empty())
		{
			searchPaths.push_back(fs::absolute(slangOutDir).string());
		}
		searchPaths.push_back(rootPath.string());
		for (const std::string& dir : includeDirs)
		{
			searchPaths.push_back(fs::absolute(dir).string());
		}
		std::vector<const char*> searchPathPtrs;
		for (const std::string& p : searchPaths)
		{
			searchPathPtrs.push_back(p.c_str());
		}

		slang::SessionDesc session{};
		session.targets         = &target;
		session.targetCount     = 1;
		session.searchPaths     = searchPathPtrs.data();
		session.searchPathCount = (SlangInt)searchPathPtrs.size();

		ComPtr<slang::ISession> slangSession;
		if (SLANG_FAILED(globalSession->createSession(session, slangSession.writeRef())))
		{
			std::cerr << "error: failed to create Slang session\n";
			return 1;
		}

		ComPtr<slang::IBlob> diagnostics;
		slang::IModule*      module = slangSession->loadModuleFromSourceString(
			moduleLoadName.c_str(),
			selfPath.string().c_str(),
			source.c_str(),
			diagnostics.writeRef());
		ReportDiagnostics(diagnostics.get());
		if (!module)
		{
			std::cerr << std::format("error: failed to load module '{}'\n", moduleLoadName);
			return 1;
		}

		diagnostics.setNull();
		slang::ProgramLayout* layout = module->getLayout(0, diagnostics.writeRef());
		ReportDiagnostics(diagnostics.get());
		if (!layout)
		{
			std::cerr << "error: failed to get program layout\n";
			return 1;
		}

		// A second session on a real GPU target, used only to reflect the ScalarDataLayout
		// StructuredBuffer element layout of handle-bearing structs (see ReflectStruct). The host
		// target above cannot instantiate a StructuredBuffer, and its pointer-sized handles would
		// mislay those structs anyway.
		slang::TargetDesc scalarTarget{};
		scalarTarget.format                  = SLANG_DXIL;
		scalarTarget.profile                 = globalSession->findProfile("sm_6_6");
		slang::SessionDesc scalarSessionDesc = session;
		scalarSessionDesc.targets            = &scalarTarget;
		scalarSessionDesc.targetCount        = 1;

		ComPtr<slang::ISession> scalarSlangSession;
		diagnostics.setNull();
		if (SLANG_FAILED(
				globalSession->createSession(scalarSessionDesc, scalarSlangSession.writeRef())))
		{
			std::cerr << "error: failed to create scalar-layout Slang session\n";
			return 1;
		}
		slang::IModule* scalarModule = scalarSlangSession->loadModuleFromSourceString(
			moduleLoadName.c_str(),
			selfPath.string().c_str(),
			source.c_str(),
			diagnostics.writeRef());
		ReportDiagnostics(diagnostics.get());
		diagnostics.setNull();
		slang::ProgramLayout* scalarLayout =
			scalarModule ? scalarModule->getLayout(0, diagnostics.writeRef()) : nullptr;
		if (!scalarLayout)
		{
			std::cerr << "error: failed to get scalar program layout\n";
			return 1;
		}

		std::vector<slang::DeclReflection*> decls;
		CollectStructDecls(module->getModuleReflection(), decls);

		std::set<std::string>         referenced;
		std::map<std::string, size_t> enumSizes;
		std::vector<StructInfo>       structs;
		for (slang::DeclReflection* decl : decls)
		{
			structs.push_back(ReflectStruct(
				decl,
				layout,
				scalarLayout,
				referenced,
				enumSizes,
				metalLayout,
				isPublic));
		}

		std::vector<EnumInfo> enums = ParseEnums(source);
		for (EnumInfo& e : enums)
		{
			// An explicit `: uintN_t` in the source wins; otherwise take the size a struct field
			// reflected for this enum (if any), else fall back to a 4-byte uint32_t.
			if (!e.declaredUnderlying.empty())
			{
				// Map the Slang scalar keyword to its C++ spelling (`uint` -> `uint32_t`); the
				// fixed-width names (uint8_t/uint16_t/...) pass through unchanged.
				e.underlying = ConstTypeToCpp(e.declaredUnderlying);
				e.size       = SizeForUnderlying(e.declaredUnderlying);
			}
			else
			{
				if (auto it = enumSizes.find(e.name); it != enumSizes.end())
				{
					e.size = it->second;
				}
				e.underlying = UnderlyingForSize(e.size);
			}
		}

		std::vector<ConstantInfo> constants = ParseConstants(source);

		if (!slangOutDir.empty())
		{
			const fs::path slangOut = fs::path(slangOutDir) / rel;
			WriteFile(slangOut, Banner(rel.generic_string()) + "\n" + source);
		}

		if (structs.empty() && enums.empty() && constants.empty())
		{
			std::cerr << std::format(
				"note: no structs, enums, or constants in {}, skipping C++ header\n",
				input);
			return 0;
		}

		const std::string cpp = EmitCpp(
			rel.generic_string(),
			ns,
			constants,
			enums,
			structs,
			referenced,
			ParseImports(source));
		WriteFile(cppOut, std::move(cpp));

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
}
