#include <bgpu/reflection/ReflectedLayout.h>
#include <bgpu/uniforms/UniformValueType.h>
#include <bgpu/uniforms/UniformsBase.h>
#include <catch2/catch_test_macros.hpp>
#include <core/glm.h>
#include <cstdint>
#include <memory>

#if !defined(_WIN32)
#	include <csignal>
#	include <sys/wait.h>
#	include <unistd.h>
#endif

namespace
{
	constexpr uint32_t c_PlanesOffset = 16;
	constexpr uint32_t c_MirrorSize   = c_PlanesOffset + 6 * 16;

	bgpu::ReflectedLayout
	Float4Layout()
	{
		bgpu::ReflectedLayout layout;
		layout.kind      = bgpu::UniformType::kValue;
		layout.valueType = bgpu::UniformValueType::kFloat4;
		layout.size      = 16;
		return layout;
	}

	// `{ float4 head; float4 planes[6]; }`; a stride of 96 is #979's whole-array stride.
	std::shared_ptr<const bgpu::ReflectedLayout>
	PlanesLayout(uint32_t planeStride)
	{
		bgpu::ReflectedLayout planes;
		planes.kind        = bgpu::UniformType::kArray;
		planes.size        = 6 * planeStride;
		planes.arrayCount  = 6;
		planes.arrayStride = planeStride;
		planes.element.push_back(Float4Layout());

		auto root  = std::make_shared<bgpu::ReflectedLayout>();
		root->kind = bgpu::UniformType::kStruct;
		root->size = c_MirrorSize;
		root->fields.push_back({ .name = "head", .offset = 0, .layout = Float4Layout() });
		root->fields.push_back({ .name = "planes", .offset = c_PlanesOffset, .layout = planes });
		return root;
	}

#if !defined(_WIN32)
	// core::ensure terminates, so the access runs in a child. A throw exits 2 instead of
	// aborting, so a type or node check firing first cannot pass for the bounds check.
	template <typename Access>
	int
	ExitStatusOfAChild(Access access)
	{
		const pid_t child = fork();
		REQUIRE(child >= 0);

		if (child == 0)
		{
			// Catch2's handler would report the expected abort as a failure from the child.
			std::signal(SIGABRT, SIG_DFL);
			try
			{
				access();
			}
			catch (...)
			{
				_exit(2);
			}
			_exit(0);
		}

		int status = 0;
		REQUIRE(waitpid(child, &status, 0) == child);
		return status;
	}

	template <typename Access>
	bool
	Aborts(Access access)
	{
		const int status = ExitStatusOfAChild(access);
		return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
	}

	template <typename Access>
	bool
	Completes(Access access)
	{
		const int status = ExitStatusOfAChild(access);
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}
#endif
}

TEST_CASE("A uniforms write that ends on the mirror's last byte lands", "[uniforms][bounds]")
{
	bgpu::UniformsBase uniforms(PlanesLayout(16), c_MirrorSize);

	uniforms["planes"][5] = glm::vec4(1.0f, 2.0f, 3.0f, 4.0f);

	CHECK(uniforms["planes"][5].GetOffset() + sizeof(glm::vec4) == c_MirrorSize);
	CHECK(static_cast<glm::vec4>(uniforms["planes"][5]) == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
}

#if !defined(_WIN32)
TEST_CASE("A uniforms access past the mirror aborts the process", "[uniforms][bounds]")
{
	SECTION("A value whose reflected offset lies beyond the mirror")
	{
		CHECK(Aborts([] {
			bgpu::UniformsBase uniforms(PlanesLayout(96), c_MirrorSize);
			uniforms["planes"][1] = glm::vec4(1.0f);
		}));
	}

	SECTION("A value straddling the mirror's end")
	{
		CHECK(Aborts([] {
			bgpu::UniformsBase uniforms(PlanesLayout(16), c_MirrorSize - 8);
			uniforms["planes"][5] = glm::vec4(1.0f);
		}));
	}

	SECTION("A read past the mirror")
	{
		CHECK(Aborts([] {
			const bgpu::UniformsBase    uniforms(PlanesLayout(96), c_MirrorSize);
			[[maybe_unused]] const auto value = static_cast<glm::vec4>(uniforms["planes"][1]);
		}));
	}

	SECTION("The in-bounds element of the same layout")
	{
		CHECK(Completes([] {
			bgpu::UniformsBase uniforms(PlanesLayout(96), c_MirrorSize);
			uniforms["planes"][0] = glm::vec4(1.0f);
		}));
	}
}
#endif
