#include <bgl_common/SurfaceReflection.h>

#include <bgl/SurfaceType.h>
#include <bgl/error.h>
#include <bgl/glm.h>
#include <bgl_common/SlangReflection.h>
#include <bgl_common/idl/GameSurfaceRecord.h>

#include <algorithm>
#include <cstdint>
#include <format>
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
			slang::IModule*       module,
			slang::ProgramLayout* layout,
			std::string_view      name)
		{
			slang::TypeReflection* iface = layout->findTypeByName(c_SurfaceInterface);
			if (iface == nullptr)
			{
				throw ApiError(
					std::format(
						"surface '{}': the module does not import bgl.SurfaceSource",
						name));
			}

			std::vector<slang::DeclReflection*> structs;
			CollectStructDecls(module->getModuleReflection(), structs);

			slang::TypeReflection* found = nullptr;
			for (slang::DeclReflection* decl : structs)
			{
				slang::TypeReflection* type = decl->getType();
				if (!layout->isSubType(type, iface))
					continue;

				if (found != nullptr)
				{
					throw ApiError(
						std::format(
							"surface '{}': '{}' and '{}' both conform to {}; a file declares one",
							name,
							FullTypeName(found),
							FullTypeName(type),
							c_SurfaceInterface));
				}
				found = type;
			}

			if (found == nullptr)
			{
				throw ApiError(
					std::format(
						"surface '{}': no struct in the module conforms to {}",
						name,
						c_SurfaceInterface));
			}
			return found;
		}

		// The parameters as this target reads them back, and only this one: a raw load reconstructs
		// its type under the buffer-element rules of the backend it was emitted for, and those
		// differ -- MSL aligns a float3 to 16 where the scalar rules leave it at 4. bgl_idlgen
		// mirrors its own structs per backend for the same reason, and refuses a committed one
		// where the two disagree. A surface is reflected rather than mirrored, so it needs no such
		// rule and gets no say in the layout.
		slang::TypeLayoutReflection*
		ParamsLayoutOf(
			slang::ProgramLayout*  layout,
			slang::TypeReflection* params,
			std::string_view       name)
		{
			slang::TypeLayoutReflection* elementLayout = BufferElementLayout(layout, params);
			if (elementLayout == nullptr)
			{
				throw ApiError(
					std::format(
						"surface '{}': failed to lay out '{}' as a record's parameters",
						name,
						FullTypeName(params)));
			}
			return elementLayout;
		}

		// The declared type is what says a field is a slot rather than a value, so the match is on
		// the contract's type names and there is no attribute to read.
		bool
		SlotKindOf(std::string_view typeName, SurfaceSlotKind& kind)
		{
			if (typeName == "ColorSlot")
				kind = SurfaceSlotKind::kColor;
			else if (typeName == "DataSlot")
				kind = SurfaceSlotKind::kData;
			else if (typeName == "NormalSlot")
				kind = SurfaceSlotKind::kNormal;
			else if (typeName == "CoverageSlot")
				kind = SurfaceSlotKind::kCoverage;
			else
				return false;

			return true;
		}

		SurfaceParamType
		ParamTypeOf(
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
				throw ApiError(
					std::format(
						"surface '{}': parameter '{}' is not a float or a float vector",
						surfaceName,
						fieldName));
			}

			return static_cast<SurfaceParamType>(
				static_cast<uint32_t>(SurfaceParamType::kFloat) + componentCount - 1);
		}

		glm::vec4
		DefaultOf(slang::VariableReflection* var, SurfaceParamType type)
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
					std::min<uint32_t>(SurfaceParamComponents(type), attribute->getArgumentCount());
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

	SurfaceType
	ReflectSurface(slang::IModule* module, std::string_view name, SlangInt targetIndex)
	{
		Slang::ComPtr<slang::IBlob> diagnostics;
		slang::ProgramLayout*       layout = module->getLayout(targetIndex, diagnostics.writeRef());
		if (layout == nullptr)
		{
			const char* text = diagnostics != nullptr ?
			                       static_cast<const char*>(diagnostics->getBufferPointer()) :
			                       "no diagnostic";
			throw ApiError(
				std::format("surface '{}': failed to lay out its module: {}", name, text));
		}

		slang::TypeReflection* surface = FindSurfaceStruct(module, layout, name);

		const std::string      paramsName = FullTypeName(surface) + ".Params";
		slang::TypeReflection* params     = layout->findTypeByName(paramsName.c_str());
		if (params == nullptr)
		{
			throw ApiError(std::format("surface '{}': failed to resolve '{}'", name, paramsName));
		}

		slang::TypeLayoutReflection* paramsLayout = ParamsLayoutOf(layout, params, name);

		SurfaceType reflected;
		reflected.name       = std::string(name);
		reflected.paramsSize = static_cast<uint32_t>(paramsLayout->getStride());

		for (unsigned i = 0; i < paramsLayout->getFieldCount(); ++i)
		{
			slang::VariableLayoutReflection* field     = paramsLayout->getFieldByIndex(i);
			const char*                      fieldName = field->getName();
			const std::string_view           spelling =
				fieldName != nullptr ? std::string_view(fieldName) : std::string_view();
			const uint32_t offset = static_cast<uint32_t>(field->getOffset());

			slang::TypeReflection* type     = field->getTypeLayout()->getType();
			const char*            typeName = type->getName();

			SurfaceSlotKind kind = SurfaceSlotKind::kColor;
			if (typeName != nullptr && SlotKindOf(typeName, kind))
			{
				if (reflected.slots.size() == idl::cGameSurfaceSlots)
				{
					throw ApiError(
						std::format(
							"surface '{}': slot '{}' is past the {} a record carries",
							name,
							spelling,
							idl::cGameSurfaceSlots));
				}

				SurfaceSlot slot;
				slot.name   = std::string(spelling);
				slot.kind   = kind;
				slot.index  = static_cast<uint32_t>(reflected.slots.size());
				slot.offset = offset;
				reflected.slots.emplace_back(std::move(slot));
				continue;
			}

			SurfaceParam param;
			param.name         = std::string(spelling);
			param.type         = ParamTypeOf(type, name, spelling);
			param.offset       = offset;
			param.defaultValue = DefaultOf(field->getVariable(), param.type);
			reflected.parameters.emplace_back(std::move(param));
		}

		return reflected;
	}
}
