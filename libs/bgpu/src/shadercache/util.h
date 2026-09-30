#pragma once
#include <bgpu/reflection/ReflectedLayout.h>

#include <core/io/ByteReader.h>
#include <core/io/ByteWriter.h>

// The reflection half of a renderer's shader cache entry. The store, the salt and the key are the
// GPU context's (bgpu::ProgramCache); what an entry contains is each backend's, and a constant
// buffer's layout is the part both backends write the same way. See docs/shader_cache.md.
namespace bgpu::shader_cache
{
	void
	WriteLayout(core::io::ByteWriter& writer, const ReflectedLayout& layout);

	/** @throws std::runtime_error on a truncated stream, as every ByteReader read does. */
	ReflectedLayout
	ReadLayout(core::io::ByteReader& reader);
}
