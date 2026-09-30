#include "shadercache/util.h"
#include <bgpu/reflection/ReflectedLayout.h>
#include <bgpu/uniforms/UniformValueType.h>
#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>
#include <cstdint>
#include <utility>

namespace bgl::shader_cache
{
	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout)
	{
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.kind));
		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.valueType));
		writer.WritePod<uint32_t>(layout.size);
		writer.WritePod<uint32_t>(layout.arrayCount);
		writer.WritePod<uint32_t>(layout.arrayStride);

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.fields.size()));
		for (const ReflectedField& field : layout.fields)
		{
			writer.WriteString(field.name);
			writer.WritePod<uint32_t>(field.offset);
			WriteLayout(writer, field.layout);
		}

		writer.WritePod<uint32_t>(static_cast<uint32_t>(layout.element.size()));
		for (const ReflectedLayout& element : layout.element) WriteLayout(writer, element);
	}

	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader)
	{
		ReflectedLayout layout;
		layout.kind        = static_cast<UniformType>(reader.ReadPod<uint32_t>());
		layout.valueType   = static_cast<UniformValueType>(reader.ReadPod<uint32_t>());
		layout.size        = reader.ReadPod<uint32_t>();
		layout.arrayCount  = reader.ReadPod<uint32_t>();
		layout.arrayStride = reader.ReadPod<uint32_t>();

		const uint32_t fieldCount = reader.ReadPod<uint32_t>();
		layout.fields.reserve(fieldCount);
		for (uint32_t i = 0; i < fieldCount; ++i)
		{
			ReflectedField field;
			field.name   = reader.ReadString();
			field.offset = reader.ReadPod<uint32_t>();
			field.layout = ReadLayout(reader);
			layout.fields.push_back(std::move(field));
		}

		const uint32_t elementCount = reader.ReadPod<uint32_t>();
		layout.element.reserve(elementCount);
		for (uint32_t i = 0; i < elementCount; ++i) layout.element.push_back(ReadLayout(reader));

		return layout;
	}
}
