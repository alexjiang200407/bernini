# Reason
We don't want to duplicate the structs shared shader and cpu side. We can forget to update one of them causing silent errors.

# Language Features
- idl files use slang language.
- We then turn the idl files into C++ structs. We only care about a few things
    1. Struct identifier
    2. Struct Members and their types.
    3. Imports

# Struct Example

```
// libs/bgl/idl/A.slang

import a.C;
import D;

namespace idl {
    public struct A {
        public float a;
        public float2 b;
        public Entry<C> c;
        private D d;
    };
}
```

We copy all files in libs/bgl/idl to libs/bgl/shaders/idl but add a comment

```slang
// THIS IS A FILE GENERATED FROM libs/bgl/idl/A.idl. DO NOT EDIT MANUALLY

import a.C;
import D;
import I; // interface

namespace idl {
    public struct A : I {
        public float a;
        public float2 b;
        public Entry<C> c;
        private D d;
    };
};
```

In src/idl/A.h

```slang
// THIS IS A FILE GENERATED FROM libs/bgl/idl/A.idl. DO NOT EDIT MANUALLY
// Notice that we dont include C or I. C since Entry doesn't require template arg. I since it is an interface

#include "D.h" // idl set as include directory for this target

// Always use bgl::idl namespace even if slang doesn't define idl namespace.
namespace bgl::idl {
    struct A {
        float a;
        float2 b;
        Entry c; // Not generic
        D d;
    };

    static_assert(sizeof(A) == ?); // Sanity check
};
```