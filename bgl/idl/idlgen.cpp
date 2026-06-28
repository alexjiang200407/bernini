#include <slang-com-ptr.h>
#include <slang.h>

#include <CLI/CLI.hpp>

#include <iostream>

/**
 * bgl_idlgen - generate C++ POD structs (and a banner-stamped Slang copy) from
 * an `.slang` IDL file, keeping the CPU and GPU definitions of a struct in sync.
 *
 * The struct layout is taken from Slang's reflection of a *host* target, which
 * lays types out with C/C++ (scalar) rules. That is what lets the generated
 * `offsetof`/`sizeof` static_asserts hold against the emitted C++ struct, and it
 * matches the layout a scalar StructuredBuffer uses on the GPU side.
 *
 * A module's identity is its path relative to --src-root, and that one path
 * drives both output locations and every reference to the module. The input is
 * written under each output root at the SAME relative subpath, so the Slang
 * import path, the .slang location, the C++ #include, and the .h location stay
 * in lockstep and cannot drift. To move a module, rename it (change its relative
 * path) so importers follow it; never relocate it to a path that disagrees with
 * its import name.
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
		std::string type;  // includes any trailing array suffix, e.g. "float[4]"
		std::string name;
		size_t      offset;
	};

	struct StructInfo
	{
		std::string            name;
		size_t                 size;
		std::vector<FieldInfo> fields;
	};

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
		slang::DeclReflection* decl,
		slang::ProgramLayout*  layout,
		std::set<std::string>& referenced)
	{
		slang::TypeReflection*       type    = decl->getType();
		slang::TypeLayoutReflection* tlayout = layout->getTypeLayout(type);

		StructInfo info;
		info.name = StripName(type->getName());
		info.size = tlayout->getSize();

		const unsigned fieldCount = tlayout->getFieldCount();
		for (unsigned i = 0; i < fieldCount; ++i)
		{
			slang::VariableLayoutReflection* var   = tlayout->getFieldByIndex(i);
			slang::TypeReflection*           ftype = var->getTypeLayout()->getType();

			// Unwrap fixed-size arrays into a C++ subscript suffix.
			std::string arraySuffix;
			while (ftype->getKind() == TypeKind::Array)
			{
				arraySuffix += std::format("[{}]", ftype->getElementCount());
				ftype = ftype->getElementType();
			}

			FieldInfo field;
			field.name   = var->getName();
			field.type   = CppBaseType(ftype, referenced) + arraySuffix;
			field.offset = var->getOffset();
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

	std::string
	Banner(const std::string& from)
	{
		return std::format("// THIS IS A FILE GENERATED FROM {}. DO NOT EDIT MANUALLY\n", from);
	}

	std::string
	EmitCpp(
		const std::string&                        bannerFrom,
		const std::string&                        ns,
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

		for (const StructInfo& s : structs)
		{
			out += std::format("\tstruct {}\n\t{{\n", s.name);
			for (const FieldInfo& f : s.fields)
			{
				out += std::format("\t\t{} {};\n", f.type, f.name);
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
	WriteFile(const fs::path& path, const std::string& content)
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

		std::set<std::string>   referenced;
		std::vector<StructInfo> structs;
		for (slang::DeclReflection* decl : decls)
		{
			structs.push_back(ReflectStruct(decl, layout, referenced));
		}

		if (structs.empty())
		{
			std::cerr << std::format("note: no structs in {}, skipping C++ header\n", input);
			return 0;
		}

		const std::string cpp =
			EmitCpp(rel.generic_string(), ns, structs, referenced, ParseImports(source));
		WriteFile(cppOut, cpp);

		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "error: " << e.what() << "\n";
		return 1;
	}
}
