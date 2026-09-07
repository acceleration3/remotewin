# remotewin

Mirror a guest Windows application's windows on a Linux host. One process per
guest application; the windows are the host's own.

Two ends, one wire.

    proto/   the wire: message ids, a writer, a reader, a stream decoder
    log/     the logger both ends use
    guest/   remotewin-guest.exe — enumeration, capture, input, cursors
    host/    remotewin — the Qt windows, and what the guest's chrome means

`proto/` is the contract between the two ends and is tested host-native: both
must agree byte for byte, and when they stop agreeing nothing fails loudly.

The guest half cross-builds with mingw-w64 (`cmake/mingw-w64.cmake`), the host
half natively. Anything under `proto/` and `log/` compiles for both, so neither
may reach for something only one toolchain has.

## C++ rules

**Files.** One class per header, named after the class; nested and private
helper types stay with their owner. Every header compiles standalone and
includes what it names - no transitive includes. Forward declare wherever the
type is only a pointer, reference, or parameter/return of a declaration; include
it for a base class, a by-value member, or `sizeof`. Never `using namespace` at
header scope.

**Before writing a function, grep for it.** Same behavior under a different
name already in the repo is the default case, not the rare one. Reuse it.

**No free-function or static-only dumping grounds.** A `Utils`/`Helper` header
is a class that has not been found yet: if functions thread the same config,
buffer, handle, or path through their parameter lists, that parameter is a
member - make it a class and inject its dependencies at construction.
Genuinely stateless functions over their arguments (`clamp`, `to_string`,
operators, ADL hooks) stay free functions. Do not wrap those in a class.

**One responsibility per class**, statable in a sentence with no "and". A method
that touches no member of its own class belongs on another class. Parameters
that always travel together are a struct.

**No speculative structure.** No interface with one implementation, no template
with one instantiation, no factory for one product, no config flag with one
setting, no virtual hook nothing overrides. Add it when the second case shows
up, not before.

**Dispatch**: known at compile time -> template or policy parameter. Chosen at
runtime -> virtual interface. Never a bool parameter selecting behavior.

**Comments: default is none.** Write *why*, never *what* - a constraint, a
workaround with its link, a tradeoff, a unit or invariant not in the type. No
restating the next line, no section banners, no Doxygen echoing parameter names,
no "Step 1/2/3" narration. Unclear code gets a better name or a split, not a
comment explaining it. Never leave commented-out code; git has it.

**Names**: no `Manager`, `Helper`, `Utils`, `Handler`, `Impl`, `data`, `info`,
`result`, `temp`, `process()`, `handle()`, `doWork()`. Name what it is or does.
Magic numbers and repeated literals are named constants at the scope owning
them. Functions stay under ~50 lines and 3 nesting levels - guard clauses and
early return first, extract second.

Types are `PascalCase`, functions and variables `snake_case`, private members
carry a trailing underscore, constants are `kPascalCase`. Namespace is `rwin`,
one nested namespace per layer (`rwin::proto`, `rwin::guest`, `rwin::host`).

**Errors**: one convention per layer. `std::optional` for "no value"; a caller
that needs the reason gets it through an explicit out-parameter or a small
result struct, because this is C++20 and `std::expected` is not available.
Assert or throw for broken preconditions. Never swallow a failure - no empty
`catch`, no ignored return, no `catch (...)`; catch by `const&`. Error messages
name the window, the file, or the value. Cleanup is RAII, never repeated per
branch. Exceptions do not cross the transport boundary: a malformed frame from
the other end is data, and is reported, not thrown.

**Ownership**: rule of zero is the target - own nothing raw, declare none of the
five. Owning a raw resource (a `HANDLE`, an fd, a mapping) means wrapping it in
a handle type; declaring one of the five means declaring all of them
consistently. Polymorphic base gets a virtual (or protected non-virtual)
destructor. Data members are private; expose the operation callers actually
perform, not a getter/setter pair. `const`, `explicit`, `[[nodiscard]]`,
`noexcept` wherever they hold.

**Allocation**: do not allocate what a fixed maximum makes an `std::array`, what
a `string_view`/`span` can view, or what a template parameter can pass instead
of `std::function`. Containers hoist out of loops and `.clear()`; known sizes
`.reserve()`. `std::map` used only for lookups after a fill phase is a sorted
`vector`. The frame path allocates nothing per frame - a pixel buffer is
acquired once and reused.

**Move vs copy** - every non-trivial copy is deliberate or it is a bug:
- Sink parameters (constructor/setter that stores) take **by value and
  `std::move` into the member**. Not `const&` + copy.
- Last use of a named local handed onward -> `std::move`. `push_back`/`insert`
  of a dying value -> `std::move(x)`; `emplace_back` does not move for you.
- Move constructors and moves of members are `noexcept`, or `vector` realloc
  copies every element.
- Never `std::move` a local on return - it blocks NRVO. Never read a moved-from
  value, never move the same object twice, never `std::move` a forwarding
  reference (`std::forward`), and never `std::move` a `const` or trivially
  copyable value - that is noise.

**Tests**: non-trivial logic ships with one runnable check. Every bug fix gets
the test that would have caught it. `src/proto/` is tested host-native
regardless of which side ships it - both ends must agree byte for byte, and
when they stop agreeing nothing fails loudly.
