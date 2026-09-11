#!/usr/bin/env python3
"""Send a C++ symbol search to the language server instead of to grep.

A PreToolUse hook on Bash/PowerShell. It **refuses** a search that is plainly a C++
symbol question -- a bare identifier, or an alternation of them, aimed at the C++
sources -- because advice that arrives beside the results it was meant to prevent is
advice that gets skimmed. Exit 2 blocks the call and sends stderr back to the model,
the same idiom gh_guard uses, so there is no output to read past and the way forward
is the LSP or a stated override.

**Where** the search points decides it, never what the pattern looks like:

  refuse   a bare identifier reaching the C++ sources -- a .cpp/.h file, a directory
           under libs/, apps/ or examples/, or no path at all.
  silence  everything else, which is most things.

The pattern is deliberately not consulted. Its shape cannot say which language it is:
STYLE.md gives core/, core/containers/ and core/str/ a lower_case domain, so
`hash_string` is C++ spelled exactly as the CMake function `enable_coverage` is, and
Slang is PascalCase exactly as C++ is. A rule read off the name therefore exempts a
whole naming domain or refuses a whole language -- both were tried, and both were
wrong. So a sweep into a tree holding C++ is asked about instead.

grep keeps every job that names something other than C++: prose in docs/, a key in a
.json or .bmaterial, a CMakeLists.txt, a .slang file or any shaders/ tree (Slang has
no server here). Those are exempt by path and always were.

`scripts/bgrep` is the answer when the search was not a C++ symbol question after all
-- a completeness sweep before a rename, a Slang identifier, a CMake name swept across
a subsystem, a string literal. It is grep: it `exec`s straight into it, so the process,
the arguments, the output, the exit status and the speed are grep's, and the only cost
is one fork ahead of the search. Reaching for it by name is a decision, which is the
whole point; reading a paragraph is not.

`sed -i` over a C++ source is refused for the same reason and points at Edit. Other
files are none of this hook's business -- editing a CMakeLists.txt or a .json with sed
is fine and stays fine.

See the root CLAUDE.md, "Read the code with clangd, not with grep".
"""

import json
import os
import re
import shlex
import sys

SEPARATORS = ("&&", "||", ";", "|", "\n")

SEARCH_TOOLS = ("grep", "egrep", "fgrep", "rg", "ag", "ack")

# grep needs -r to walk a tree; these walk the working directory with no flag at all, so a
# bare `rg Symbol` reaches the sources exactly as `grep -rn Symbol` does.
WALKS_BY_DEFAULT = ("rg", "ag", "ack")

# A C++ identifier and nothing else: no regex metacharacters, no spaces. A pattern
# with any syntax in it is asking something the LSP cannot answer anyway.
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# `A\|B` (BRE), `A|B` (ERE/rg). Every branch has to be a bare identifier for the whole
# to count as one question -- `Foo\|^struct` is a regex and stays grep's.
ALTERNATION = re.compile(r"\\\||\|")

# Worth at least two segments, so a search for `i` or `Get` is not lectured at.
MIN_LENGTH = 4

# Where C++ symbols live. A path outside these is prose, a shader or build files.
CPP_ROOTS = ("libs", "apps", "examples")

CPP_SUFFIXES = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".inl")

# The way past a refusal: `scripts/bgrep`, which is grep under a name that says the search
# was meant. A real program rather than a flag this file strips, so it behaves the same with
# the hook off, outside this checkout and in anyone's shell -- `grep --force` was neither, and
# a caller who believed in it had a command that fails everywhere else.
#
# It needs no handling here. Its basename is not one this looks for, so it is simply never a
# search this refuses.
OVERRIDE = "scripts/bgrep"

# `>`, `>>`, `2>`, `&>`, `<`, and the `>&` of `2>&1`: what follows one is a file or a
# descriptor, not a path being searched. Unrecognised, a `> dump.cpp` reads as a C++ search
# target. `&` belongs in both classes -- shlex splits `2>&1` into `2`, `>&`, `1`, so the
# operator ends in `&` rather than in the angle bracket.
REDIRECT = re.compile(r"^[0-9&]*[<>]+[&]?$")

# Shaders are not C++ and have no language server here, so a path inside one is grep's.
SHADER_DIRS = ("shaders",)

# The value of these is the pattern itself, so it is what gets inspected.
PATTERN_FLAGS = {"-e", "--regexp", "-f", "--file"}

# The value of these is a count, a glob, a type or a colour mode -- never the pattern, and
# never a path. Consumed and dropped, or `-A 3` would offer `3` as the symbol to look up.
VALUE_FLAGS = {"-m", "--max-count", "-A", "-B", "-C", "--after-context", "--before-context",
               "--context", "--include", "--exclude", "--glob", "-g", "-t", "--type",
               "--color", "--colour", "-M", "--max-columns"}


REFUSAL = (
    "Refused: '{symbol}' looks like a C++ symbol and this searches the C++ sources. Use the LSP.\n"
    "\n"
    "    findReferences   who calls it / what breaks if it changes\n"
    "    goToDefinition   where it is defined\n"
    "    hover            what it is and what it returns\n"
    "    workspaceSymbol  where '{symbol}' is, when the file is not known yet\n"
    "\n"
    "It knows a declaration from a definition, follows a typealias, ignores a comment that\n"
    "happens to say the name, and never answers with another class's identically-named\n"
    "method. See the root CLAUDE.md, \"Read the code with clangd, not with grep\".\n"
    "\n"
    "`{override}` runs it instead -- that is grep, reached by a name that says the search was\n"
    "meant, so the results are the ones you asked for. Three cases earn it rather than argue:\n"
    "\n"
    "  - completeness decides the change -- a rename, a removed enumerator, proving a call\n"
    "    site is the last one -- because clangd's index lags the working tree.\n"
    "  - '{symbol}' is a Slang identifier. Slang has no language server here, so nothing\n"
    "    above can answer it and grep is the only tool there is.\n"
    "  - '{symbol}' is not a symbol at all -- a string literal, a comment, a log line."
)

SED_REFUSAL = (
    "Refused: editing a C++ source with sed. Use Edit, which matches exactly and fails loudly\n"
    "when it does not.\n"
    "\n"
    "    {files}\n"
    "\n"
    "sed -i writes a regex substitution nothing checks: a pattern that matches twice edits\n"
    "twice, and one that matches nothing succeeds silently. Other files are not this rule's\n"
    "business -- a CMakeLists.txt, a .json or a .md is fine. There is no bsed: the answer\n"
    "to a mechanical edit across many C++ files is a script that reads and writes them, not a\n"
    "regex nobody checked."
)


def segments(command):
    """The command split into separately-executed pieces, each as tokens.

    Tokenised before it is split, never the other way round: `|` separates two commands but
    also spells alternation inside a pattern, and splitting the raw string tears
    `"toMatrix\\|formatSize"` in half -- which is how an alternation of symbols, the most
    natural way to ask findReferences of several names at once, went unnoticed.
    """
    lexer = shlex.shlex(command, posix=False, punctuation_chars=True)
    lexer.whitespace_split = True
    try:
        tokens = list(lexer)
    except ValueError:
        tokens = command.split()

    current = []
    skip_next = False
    for token in tokens:
        if skip_next:
            skip_next = False
            continue
        if REDIRECT.match(token):
            # `2>&1` reaches here as `2`, `>&`, `1`: the descriptor in front of the operator
            # has already been appended, and left there it is a stray path. Only when it was
            # written against the operator, though -- `2>out` is a redirect and `2 > out` is
            # an argument named `2` beside one, and shlex has dropped the space that says
            # which. The raw command still has it.
            if current and current[-1].isdigit() and f"{current[-1]}>" in command:
                current.pop()
            skip_next = True
            continue
        if token in SEPARATORS:
            if current:
                yield [t.strip("\"'") for t in current]
            current = []
            continue
        current.append(token)
    if current:
        yield [t.strip("\"'") for t in current]


def is_recursive(args):
    """Whether the search walks a tree rather than filtering stdin.

    By flag character rather than by whole token: -r, -R, -rn, -Rn and -nr all mean it, and
    enumerating the spellings misses whichever one nobody thought of.
    """
    for arg in args:
        if not arg.startswith("-") or arg.startswith("--"):
            continue
        if "r" in arg[1:] or "R" in arg[1:]:
            return True
    return "--recursive" in args


def normalize(path):
    """A path in one shape. Empty for `.` and `./`, which name no path in particular."""
    return path.replace("\\", "/").lstrip("./")


def names_cpp_file(path):
    """Whether one operand names C++ sources outright, by suffix."""
    return normalize(path).endswith(CPP_SUFFIXES)


def is_cpp_tree(path):
    """Whether one operand is a directory C++ lives in.

    A directory sweep is judged by where it points and not by what it is for: libs/, apps/
    and examples/ hold CMakeLists.txt and .md beside the sources, and no reading of the
    pattern can say which of them a search meant. So the sweep is refused and `scripts/bgrep`
    is how a caller says it meant the other thing. A *named* non-C++ file needs no such thing
    -- it has already said so.
    """
    normalized = normalize(path)
    if not normalized:
        return False

    # A named file of some other kind -- .slang, .md, .json, .txt, CMakeLists.txt -- is not
    # the LSP's to answer, even under libs/. Only an extensionless path is taken for a
    # directory.
    _, _, tail = normalized.rpartition("/")
    if "." in tail:
        return False

    segments_of = normalized.split("/")
    if any(part in SHADER_DIRS for part in segments_of):
        return False

    return segments_of[0] in CPP_ROOTS



def symbol_of(args):
    """The bare C++ identifier a search is for, or None if it is not one.

    An alternation of identifiers counts and reports its first branch: `A\\|B\\|C` across the
    sources is one findReferences question asked three times, which is exactly the case a
    regex-only test lets through.
    """
    operands = []
    keep_next = False
    drop_next = False
    for arg in args:
        if keep_next:
            keep_next = False
            operands.append(arg)
            continue
        if drop_next:
            drop_next = False
            continue
        if arg in PATTERN_FLAGS:
            keep_next = True
            continue
        if arg in VALUE_FLAGS:
            drop_next = True
            continue
        operands.append(arg)

    positional = [o for o in operands if not o.startswith("-")]
    if not positional:
        return None, []

    branches = [b for b in ALTERNATION.split(positional[0]) if b]
    if not branches:
        return None, []
    for branch in branches:
        if not IDENTIFIER.match(branch) or len(branch) < MIN_LENGTH:
            return None, []

    return branches[0], positional[1:]


def tool_args(tokens, name):
    """The arguments following the first invocation of `name` in `tokens`, or None."""
    for i, token in enumerate(tokens):
        base = os.path.basename(token.replace("\\", "/")).lower()
        if base in (name, f"{name}.exe"):
            return tokens[i + 1:]
    return None


def sed_targets(tokens):
    """The C++ sources an in-place sed would rewrite."""
    args = tool_args(tokens, "sed")
    if args is None:
        return []

    in_place = any(
        arg.startswith("-i") or arg == "--in-place" or
        (arg.startswith("-") and not arg.startswith("--") and "i" in arg[1:])
        for arg in args
    )
    if not in_place:
        return []

    return [a for a in args if not a.startswith("-") and names_cpp_file(a)]



def verdict(command):
    """The symbol a search reaching the C++ sources is for, or None."""
    for tokens in segments(command):
        for name in SEARCH_TOOLS:
            args = tool_args(tokens, name)
            if args is None:
                continue
            if name == "rg" and "--files" in args:
                continue  # file discovery has no content pattern
            symbol, operands = symbol_of(args)
            if not symbol:
                continue

            paths = [p for p in operands if normalize(p)]

            # A named C++ file, a directory C++ lives in, or no path at all -- which from
            # anywhere in this repo reaches one. What the pattern looks like is deliberately
            # not consulted: see OVERRIDE.
            reaches_cpp = (
                any(names_cpp_file(p) for p in paths)
                or any(is_cpp_tree(p) for p in paths)
                or (not paths and (is_recursive(args) or name in WALKS_BY_DEFAULT))
            )
            if not reaches_cpp:
                continue

            return symbol
    return None


def command_of(payload):
    """The command line to inspect, or "" for any payload shape that does not carry one."""
    if not isinstance(payload, dict):
        return ""
    tool_input = payload.get("tool_input")
    if not isinstance(tool_input, dict):
        return ""
    command = tool_input.get("command")
    return command if isinstance(command, str) else ""


def main():
    # This runs ahead of every Bash call in the session, so it fails open on anything it does
    # not understand: refusing the tree because a payload had an unexpected shape would cost
    # far more than the searches it exists to catch.
    try:
        command = command_of(json.load(sys.stdin))
    except (ValueError, OSError):
        return 0

    try:
        for tokens in segments(command):
            files = sed_targets(tokens)
            if not files:
                continue
            print(SED_REFUSAL.format(files="  ".join(files)), file=sys.stderr)
            return 2

        symbol = verdict(command)
    except Exception:  # noqa: BLE001 -- see above: nothing here is worth a blocked session.
        return 0

    if not symbol:
        return 0

    print(REFUSAL.format(symbol=symbol, override=OVERRIDE), file=sys.stderr)
    return 2



if __name__ == "__main__":
    sys.exit(main())
