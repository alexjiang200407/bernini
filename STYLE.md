
# Naming domains

Two styles, and the **directory decides** which one applies — never the author's read of what kind
of thing they are writing.

| Where | Style |
|---|---|
| `core/containers/`, `core/str/` | `lower_case` throughout: these stand in for standard-library types |
| the rest of `libs/core/` | `lower_case` free functions, `PascalCase` types and methods |
| everything else | `PascalCase` types and functions |

`just tidy` checks this. The rules below are what it enforces; [docs/naming.md](docs/naming.md)
covers the split, the exemptions, and how to run it.

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
auto abcd = Type(); // auto for initialization, prioritize () over {}
int i;              // Dont use auto for primitive types
```




# Const correctness

Const correctness is enforced for
1.  Function and method parameters and returns
2.  Member variables
3.  Method qualifiers

Local variables is less strict. West const.

# Error handling

For problems caused by poor internal logic inside shared or static library, use asserts

For problems caused by the caller (invalid args), throw runtime error

Always mark functions as noexcept or not

```cpp

void ProcessData(const std::vector<int>& data) const; // west const reference for parameters

```

# Paths

Two types, and the one you choose says **who resolves the string**.

| Type | What it addresses |
|---|---|
| `std::filesystem::path` | a location on the host filesystem, resolved by the OS: may be absolute, carries the native separator, composes with `/` |
| `std::string_view` (`std::string` where it is stored) | a key into a mounted `core::file::IFileSystem`: always data-root-relative, always `/`-separated, never absolute |

So a `LooseFileSystem`'s root, a `.bpak` on disk and `write_atomic`'s target are `path`; every
`IFileSystem` method, every `load*` overload that takes a mount, and every reference a container
stores are `string_view`.

They are not interchangeable, and on Windows the failure is silent. A mount key is matched
byte-for-byte — `PakFile` resolves one through a hash map keyed on the string the archive stored — so
a key that has passed through `std::filesystem::path` arrives `\`-separated and misses. The same key
still resolves through `LooseFileSystem`, because the OS accepts either separator. Loose works,
packed does not, and nothing reports a problem.

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

# Templates

- Maximize the use of concepts over requires


# Comments (CRITICAL)

- The default is **no comment**. Keep the ones you do write as short as possible — one line where it fits.
- Only comment under these circumstances
	- There are non-obvious Pre-Conditions and Post-Conditions e.g. will throw error if xxx
	- Important Clarifications
	- Why the obvious approach was *not* taken
- **Never narrate.** No play-by-play (`// Now we do X`, `// Loop over the submeshes`), no restating the
  line below it (`// Bump the epoch` over `++m_Epoch;`), and no explaining the *change* rather than the
  code (`// This is now per-instance`, `// Moved from Submesh`). A comment describes the code as it is,
  for someone who has no idea a change ever happened — the diff and the commit message carry the rest.
- If it needs a paragraph, it belongs in `./docs`.
- Use javadoc comments for comments above functions structs e.g.

```cpp
/**
 * The first sentence is the summary description.
 * Subsequent paragraphs provide deeper technical context.
 *
 * @param parameterName Description of the method parameter.
 * @return Description of the returned value.
 */
```

- You can write more comments in test files.