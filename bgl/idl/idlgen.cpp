#include <CLI/CLI.hpp>
#include <slang-com-ptr.h>
#include <slang.h>

/**
 * bgl_idlgen - generate C++ POD structs, enums, and constants (and a
 * banner-stamped Slang copy) from an `.slang` IDL file, keeping the CPU and GPU
 * definitions in sync.
 *
 * Usage:
 *   bgl_idlgen --src-root <dir> [--cpp-out-dir <dir>] [--slang-out-dir <dir>]
 *              [--namespace ns] [-I <search-dir>]... <input.slang>
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
			throw std::runtime_error(std::format("unsupported scalar type {}", (int)s));
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
			throw std::runtime_error(std::format("unsupported vector element type {}", (int)elem));
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
			throw std::runtime_error(
				std::format("unsupported field type kind {}", (int)t->getKind()));
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
		std::string            name;
		size_t                 size;
		std::vector<FieldInfo> fields;
	};

	struct EnumInfo
	{
		std::string                                    name;
		std::string                                    underlying = "uint32_t";
		size_t                                         size       = 4;
		std::vector<std::pair<std::string, long long>> enumerators;
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
		static const std::map<std::string, std::string> kMap = {
			{ "let", "auto" },    { "int", "int32_t" },   { "uint", "uint32_t" },
			{ "float", "float" }, { "double", "double" }, { "bool", "bool" },
		};
		if (auto it = kMap.find(slangType); it != kMap.end())
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

	StructInfo
	ReflectStruct(
		slang::DeclReflection*         decl,
		slang::ProgramLayout*          layout,
		std::set<std::string>&         referenced,
		std::map<std::string, size_t>& enumSizes)
	{
		slang::TypeReflection*       type    = decl->getType();
		slang::TypeLayoutReflection* tlayout = layout->getTypeLayout(type);

		StructInfo info;
		info.name = StripName(type->getName());
		info.size = tlayout->getSize();

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

			FieldInfo field;
			field.name        = var->getName();
			field.type        = CppBaseType(ftype, referenced);
			field.arraySuffix = arraySuffix;
			field.offset      = var->getOffset();
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
			info.name = name;

			const std::string body      = source.substr(brace + 1, close - brace - 1);
			long long         nextValue = 0;
			size_t            itemStart = 0;
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
					long long   value          = nextValue;
					if (const size_t eq = item.find('='); eq != std::string::npos)
					{
						enumeratorName = item.substr(0, eq);
						if (const size_t ne = enumeratorName.find_last_not_of(" \t");
						    ne != std::string::npos)
						{
							enumeratorName = enumeratorName.substr(0, ne + 1);
						}
						try
						{
							value = std::stoll(item.substr(eq + 1), nullptr, 0);
						}
						catch (const std::exception&)
						{
							value = nextValue;
						}
					}

					if (!enumeratorName.empty())
					{
						info.enumerators.emplace_back(enumeratorName, value);
						nextValue = value + 1;
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

		// Constants first: they may be used as array bounds in enums/structs and
		// carry no dependency on anything below.
		for (const ConstantInfo& c : constants)
		{
			out += std::format("\tconstexpr {} {} = {};\n", c.type, c.name, c.value);
		}
		if (!constants.empty())
		{
			out += "\n";
		}

		// Enums first: struct fields below reference them by name, and locally
		// defined enums are not imported so they emit no #include.
		for (const EnumInfo& e : enums)
		{
			out += std::format("\tenum class {} : {}\n\t{{\n", e.name, e.underlying);
			for (const auto& [enumerator, value] : e.enumerators)
			{
				out += std::format("\t\t{} = {},\n", enumerator, value);
			}
			out += "\t};\n\n";
			out += std::format("\tstatic_assert(sizeof({}) == {});\n\n", e.name, e.size);
		}

		for (const StructInfo& s : structs)
		{
			out += std::format("\tstruct {}\n\t{{\n", s.name);
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

	void
	WriteFile(const fs::path& path, std::string content)
	{
		if (path.has_parent_path())
		{
			fs::create_directories(path.parent_path());
		}
		std::ofstream out(path, std::ios::binary);
		if (!out)
		{
			throw std::runtime_error(std::format("could not open output file {}", path.string()));
		}
		out << content;
	}

	std::string
	ReadFile(const fs::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			throw std::runtime_error(std::format("could not open input file {}", path.string()));
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
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

	app.add_option("input", input, "Input .slang IDL file")->required();
	app.add_option("--src-root", srcRoot, "Root the input's module path is relative to")
		->required();
	app.add_option("--cpp-out-dir", cppOutDir, "Output root for the generated C++ header");
	app.add_option("--slang-out-dir", slangOutDir, "Output root for the banner-stamped Slang copy");
	app.add_option("--namespace", baseNs, "Base C++ namespace for the generated structs")
		->capture_default_str();
	app.add_option("-I,--include", includeDirs, "Search directory for imported Slang modules");

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
			throw std::runtime_error(
				std::format("input {} is not under --src-root {}", input, srcRoot));
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

		if (!slangOutDir.empty())
		{
			const fs::path slangOut = fs::path(slangOutDir) / rel;
			WriteFile(slangOut, Banner(rel.generic_string()) + "\n" + source);
		}

		if (cppOutDir.empty())
		{
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
		slang::IModule*      module =
			slangSession->loadModule(moduleLoadName.c_str(), diagnostics.writeRef());
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

		std::vector<slang::DeclReflection*> decls;
		CollectStructDecls(module->getModuleReflection(), decls);

		std::set<std::string>         referenced;
		std::map<std::string, size_t> enumSizes;
		std::vector<StructInfo>       structs;
		for (slang::DeclReflection* decl : decls)
		{
			structs.push_back(ReflectStruct(decl, layout, referenced, enumSizes));
		}

		std::vector<EnumInfo> enums = ParseEnums(source);
		for (EnumInfo& e : enums)
		{
			if (auto it = enumSizes.find(e.name); it != enumSizes.end())
			{
				e.size = it->second;
			}
			e.underlying = UnderlyingForSize(e.size);
		}

		std::vector<ConstantInfo> constants = ParseConstants(source);

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
