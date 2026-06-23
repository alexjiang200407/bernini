
# Variables

```cpp
static constexpr auto c_Variable = 1; // c_ prefix for constants

int main() {
	int myVariable; // camelCase for variable names
}

int g_GlobalVariable; // g_ prefix for static/global variables

```

Try to maximize use of `auto`.

```cpp
auto myVariable = 42;
auto abcd = Type{};
```


# Const correctness

Const correctness is enforced for
1.  Function and method parameters and returns
2.  Member variables
3.  Method qualifiers

Local variables is less strict. West const.

```cpp

void ProcessData(const std::vector<int>& data) const; // west const reference for parameters

```

# Classes

```cpp

// PascalCase for class names
class MyClass {

public:
	int
	GetMemberVariable() const; // member functions in PascalCase

private:
	int m_MemberVariable; // member variables prefixed with 'm_' and camelCase
};


```

# Structs
Use structs instead of classes when the data structure is plain-old-data (POD) without complex behavior. Structs do not require the 'm_' prefix for member variables.


```cpp
struct Vec3 {
	float x;
	float y;
	float z;
};
```


# Macros

Avoid using macros except for build-specific purposes. Prefer constexpr, inline functions, or templates instead.


# File Naming

For cpp and header files use pascal case if it exports a class/struct. Use snake case for files that only export functions or variables e.g. util.h. Use snake case for directories.


# Enums

Enum keys should be in PascalCase prefixed by `k`.

```cpp

enum class Color {
	kRed,
	kGreen,
	kBlue
};

```
