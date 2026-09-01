#include "idl/MeshInstance.h"
#include "util/util.h"
#include <bgl/glm.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
	// A placement with every kind of term a TRS carries, and no symmetry: a transposed pack, a
	// dropped row and a dropped column each give a different wrong answer.
	glm::mat4
	Placement()
	{
		auto m = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -5.0f, 11.0f));
		m      = glm::rotate(m, glm::radians(37.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
		return glm::scale(m, glm::vec3(2.0f, 0.5f, 4.0f));
	}

	// What MeshInstance::TransformPoint does on the GPU, spelled out against the packed rows rather
	// than against the matrix -- so this reads the bytes the shader reads, not the source they came
	// from.
	glm::vec3
	TransformPoint(const bgl::idl::MeshInstance& instance, glm::vec3 p)
	{
		const auto h = glm::vec4(p, 1.0f);
		return glm::vec3(
			glm::dot(instance.transform[0], h),
			glm::dot(instance.transform[1], h),
			glm::dot(instance.transform[2], h));
	}
}

TEST_CASE("A placement's rows transform a point the way its matrix does", "[idl][transform]")
{
	// The one thing about this packing that can be wrong: rows or columns. Both are three float4s
	// of the same matrix, so nothing about the bytes says which -- and under an identity transform
	// the two agree, which is why a golden image of an unrotated cube would not catch it.
	const glm::mat4 m = Placement();

	auto instance = bgl::idl::MeshInstance();
	bgl::WriteInstanceTransform(instance, m);

	for (const glm::vec3 p :
	     { glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-2.0f, 7.0f, 0.5f) })
	{
		const glm::vec3 expected = glm::vec3(m * glm::vec4(p, 1.0f));
		const glm::vec3 got      = TransformPoint(instance, p);

		CHECK(got.x == Catch::Approx(expected.x));
		CHECK(got.y == Catch::Approx(expected.y));
		CHECK(got.z == Catch::Approx(expected.z));
	}
}

TEST_CASE("A placement's fourth row is not stored", "[idl][transform]")
{
	// Three rows and not four, because the fourth of an affine transform is always (0,0,0,1). The
	// size is what the shader's stride agrees with, so it is the assertion worth making.
	static_assert(sizeof(bgl::idl::MeshInstance().transform) == 3 * sizeof(glm::vec4));

	auto instance = bgl::idl::MeshInstance();
	bgl::WriteInstanceTransform(instance, glm::mat4(1.0f));

	CHECK(instance.transform[0] == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
	CHECK(instance.transform[1] == glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
	CHECK(instance.transform[2] == glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
}
