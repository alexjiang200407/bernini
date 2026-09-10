#include <bgl_common/SurfaceReflection.h>

#include <bgl/SurfaceType.h>
#include <bgl/glm.h>
#include <bgl_common/SlangReflection.h>
#include <bgl_common/idl/GameSurfaceRecord.h>

#include <algorithm>
#include <core/err/util.h>
#include <cstdint>
#include <optional>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgl
{
	namespace
	{
		constexpr const char* c_SurfaceInterface = "ISurfaceSource";

		slang::TypeReflection*
		FindSurfaceStruct(
			slang::IModule*       slangModule,
			slang::ProgramLayout* layout,
			std::string_view      surfaceName)
		{
			// Null when the module never imported the contract, which is how a module that is not a
			// surface at all says so.
			slang::TypeReflection* iface = layout->findTypeByName(c_SurfaceInterface);
			if (iface == nullptr)
				return nullptr;

			std::vector<slang::DeclReflection*> structs;
			CollectStructDecls(slangModule->getModuleReflection(), structs);

			slang::TypeReflection* found = nullptr;
			for (slang::DeclReflection* decl : structs)
			{
				slang::TypeReflection* type = decl->getType();
				if (!layout->isSubType(type, iface))
					continue;

				if (found != nullptr)
				{
					core::throw_runtime_error(
						"surface '{}': '{}' and '{}' both conform to {}; a file declares one",
						surfaceName,
						FullTypeName(found),
						FullTypeName(type),
						c_SurfaceInterface);
				}
				found = type;
			}

			if (found == nullptr)
			{
				core::throw_runtime_error(
					"surface '{}': no struct in the module conforms to {}",
					surfaceName,
					c_SurfaceInterface);
			}
			return found;
		}

		// A record is read with RawBuffer.Load<T>, which reconstructs its type from scalar loads,
		// so these are the offsets under the scalar rules -- the same ones bgl_idlgen mirrors every
		// other record under. The caller's session decides: on a DXIL target this is that layout,
		// on a Metal one it is MSL's, which belongs to a structured buffer's element and not to a
		// raw load. See the header.
		slang::TypeLayoutReflection*
		ParamsLayoutOf(
			slang::ProgramLayout*  layout,
			slang::TypeReflection* params,
			std::string_view       surfaceName)
		{
			slang::TypeLayoutReflection* elementLayout = BufferElementLayout(layout, params);
			if (elementLayout == nullptr)
			{
				core::throw_runtime_error(
					"surface '{}': failed to lay out '{}' as a record's parameters",
					surfaceName,
					FullTypeName(params));
			}
			return elementLayout;
		}

		// The declared type is what says a field is a texture rather than a value, so the match is
		// on the contract's slot type names and there is no attribute to read.
		bool
		TextureKindOf(std::string_view typeName, SurfaceTextureKind& kind)
		{
			if (typeName == "ColorSlot")
				kind = SurfaceTextureKind::kColor;
			else if (typeName == "DataSlot")
				kind = SurfaceTextureKind::kData;
			else if (typeName == "NormalSlot")
				kind = SurfaceTextureKind::kNormal;
			else if (typeName == "CoverageSlot")
				kind = SurfaceTextureKind::kCoverage;
			else
				return false;

			return true;
		}

		SurfaceValueType
		ValueTypeOf(
			slang::TypeReflection* type,
			std::string_view       surfaceName,
			std::string_view       fieldName)
		{
			using Kind = slang::TypeReflection::Kind;

			const Kind     kind = type->getKind();
			const uint32_t componentCount =
				kind == Kind::Vector ? static_cast<uint32_t>(type->getElementCount()) : 1;
			slang::TypeReflection* scalar = kind == Kind::Vector ? type->getElementType() : type;

			const bool packable =
				(kind == Kind::Scalar || kind == Kind::Vector) && componentCount <= 4 &&
				scalar->getScalarType() == slang::TypeReflection::ScalarType::Float32;
			if (!packable)
			{
				core::throw_runtime_error(
					"surface '{}': '{}' is not a float or a float vector, so nothing can set it",
					surfaceName,
					fieldName);
			}

			return static_cast<SurfaceValueType>(
				static_cast<uint32_t>(SurfaceValueType::kFloat) + componentCount - 1);
		}

		glm::vec4
		DefaultOf(slang::VariableReflection* var, SurfaceValueType type)
		{
			glm::vec4 value(0.0f);
			if (var == nullptr)
				return value;

			for (unsigned i = 0; i < var->getUserAttributeCount(); ++i)
			{
				slang::Attribute* attribute = var->getUserAttributeByIndex(i);
				const char*       name      = attribute->getName();
				if (name == nullptr || std::string_view(name) != "Default")
					continue;

				const uint32_t components =
					std::min<uint32_t>(SurfaceValueComponents(type), attribute->getArgumentCount());
				for (uint32_t c = 0; c < components; ++c)
				{
					float component = 0.0f;
					if (SLANG_SUCCEEDED(attribute->getArgumentValueFloat(c, &component)))
					{
						value[static_cast<glm::length_t>(c)] = component;
					}
				}
				break;
			}
			return value;
		}
	}

	std::optional<ReflectedSurface>
	ReflectSurface(slang::IModule* slangModule, std::string_view surfaceName, SlangInt targetIndex)
	{
		Slang::ComPtr<slang::IBlob> diagnostics;
		slang::ProgramLayout* layout = slangModule->getLayout(targetIndex, diagnostics.writeRef());
		if (layout == nullptr)
		{
			const char* text = diagnostics != nullptr ?
			                       static_cast<const char*>(diagnostics->getBufferPointer()) :
			                       "no diagnostic";
			core::throw_runtime_error(
				"surface '{}': failed to lay out its module: {}",
				surfaceName,
				text);
		}

		slang::TypeReflection* surface = FindSurfaceStruct(slangModule, layout, surfaceName);
		if (surface == nullptr)
			return std::nullopt;

		const std::string      paramsName = FullTypeName(surface) + ".MaterialParams";
		slang::TypeReflection* params     = layout->findTypeByName(paramsName.c_str());
		if (params == nullptr)
		{
			core::throw_runtime_error(
				"surface '{}': failed to resolve '{}'",
				surfaceName,
				paramsName);
		}

		slang::TypeLayoutReflection* paramsLayout = ParamsLayoutOf(layout, params, surfaceName);

		SurfaceType reflected;
		reflected.name            = std::string(surfaceName);
		reflected.params.byteSize = static_cast<uint32_t>(paramsLayout->getStride());

		for (unsigned i = 0; i < paramsLayout->getFieldCount(); ++i)
		{
			slang::VariableLayoutReflection* field     = paramsLayout->getFieldByIndex(i);
			const char*                      fieldName = field->getName();
			const std::string_view           spelling =
				fieldName != nullptr ? std::string_view(fieldName) : std::string_view();
			const uint32_t byteOffset = static_cast<uint32_t>(field->getOffset());

			slang::TypeReflection* type     = field->getTypeLayout()->getType();
			const char*            typeName = type->getName();

			SurfaceTextureKind kind = SurfaceTextureKind::kColor;
			if (typeName != nullptr && TextureKindOf(typeName, kind))
			{
				if (reflected.params.textures.size() == idl::cGameSurfaceTextureSlots)
				{
					core::throw_runtime_error(
						"surface '{}': texture '{}' is past the {} a record carries",
						surfaceName,
						spelling,
						idl::cGameSurfaceTextureSlots);
				}

				SurfaceTexture texture;
				texture.name       = std::string(spelling);
				texture.kind       = kind;
				texture.index      = static_cast<uint32_t>(reflected.params.textures.size());
				texture.byteOffset = byteOffset;
				reflected.params.textures.emplace_back(std::move(texture));
				continue;
			}

			SurfaceValue value;
			value.name         = std::string(spelling);
			value.type         = ValueTypeOf(type, surfaceName, spelling);
			value.byteOffset   = byteOffset;
			value.defaultValue = DefaultOf(field->getVariable(), value.type);
			reflected.params.values.emplace_back(std::move(value));
		}

		return ReflectedSurface{ std::move(reflected), FullTypeName(surface) };
	}
}
