#pragma once
#include "buffer/ElementType.h"

namespace gfx
{
	bool
	semanticMatches(
		std::string_view elementName,
		std::string_view semanticName,
		uint32_t         semanticIndex);

	struct ConstantBufferInput
	{
		struct Entry
		{
			std::string name   = "";
			uint32_t    offset = 0;
			ElementType type   = ElementType::kInvalid;
		};

		std::string        name  = "";
		uint32_t           slot  = 0u;
		uint32_t           space = 0u;
		uint32_t           size  = 0u;
		std::vector<Entry> entries;
	};

	struct VertexAttribute
	{
		enum Type
		{
			kInvalid = -1,
			kPosition,
			kUV,
			kNormal,
			kTangent,
			kTotal,
		};

		Type          type          = kInvalid;
		uint32_t      semanticIndex = 0u;
		std::string   semanticName  = "";
		std::string   semanticId    = "";
		nvrhi::Format format        = nvrhi::Format::UNKNOWN;
	};

	VertexAttribute::Type
	mapSemantic(const char* semantic);

	std::vector<VertexAttribute>
	getVertexAttributes(nvrhi::ShaderHandle shader);

	std::vector<ConstantBufferInput>
	getConstantBufferInputs(nvrhi::ShaderHandle shader);

}
