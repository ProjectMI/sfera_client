# Container substrate elision plan

## Scope

This pass covers the RawIDA block `0x00441000` through `0x00442400`. The block is not a game/client feature module. It is mostly shared support code from `ShareClientSeverCode`, especially `Arrays.cpp`, `inter.h`, `config.cpp`, `DataCont/sListContainer.cpp`, and generated MSVC runtime thunks. These functions must be tracked in `cut_functions_current`, but they should not be copied into the runnable client as a dedicated container library.

## Evidence from RawIDA

- `0x00441030`, `0x004410F0`, and `0x00441120` are bounds diagnostics for the legacy `AutoBoundsArray` / `BoundCheckArray` layer. Their only useful semantics are grow-to-fit, negative-index guard, and out-of-range guard.
- `0x00441150` through `0x00441840` are bit/token stream helpers used by the legacy config serializer. They are plumbing for already modeled config text loading, not a standalone engine system.
- `0x00441E20`, `0x00441E30`, `0x00441E40`, and `0x00441E50` are runtime/checking or concurrency stubs. They are toolchain artifacts and must not be reconstructed as project code.
- `0x00441F10` through `0x00442400` are typed clones around `DataCont/sListContainer.cpp`. The repeated 28/27/75 byte triads are template-like list accessors and mutators, not domain logic.

## Elision rule

Do not add `LegacyContainer`, `AutoBoundsArray`, `sListContainer`, or `BitStream` build targets. When a later real feature function requires one of these helpers, absorb the semantic directly into the owning high-level module:

1. Use `std::vector`, `std::string`, `std::deque`, or existing project resource lists at the feature boundary.
2. Preserve only externally visible behavior: ordering, capacity limits if they affect data, and fatal diagnostics if the caller depends on them.
3. Keep tiny runtime/toolchain stubs marked as `toolchain/elided` in the reconstruction map.
4. Revisit a helper only if a later caller proves that it carries client-visible state or file-format semantics.

## Practical mapping

| Address range | Legacy source hint | Decision |
| --- | --- | --- |
| `0x00441000` | path slash scan | Inline with existing string/path helpers when needed. |
| `0x00441030`-`0x00441120` | `Arrays.cpp` bounds checks | Do not port; use standard containers and local validation. |
| `0x00441150`-`0x00441840` | `inter.h` bit/token helpers | Do not port as a public module; fold into config/resource parsers only if a concrete parser needs bit-level compatibility. |
| `0x00441990`-`0x00441AC0` | `config.cpp` | Already mapped to `SferaConfig` / `SferaTextConfig`; keep as feature-level work. |
| `0x00441B70`-`0x00442400` | `DataCont/sListContainer.cpp` typed clones | Mark as elided container substrate; later feature owners should use STL-backed collections directly. |

## Follow-up slice: `0x00442450` through `0x00443960`

The next 50-function slice stays inside `DataCont/sDataContainers.cpp` and STL-style adapter code. It adds string-position guards, vector/string resize helpers, map/set and list length guards, and typed constructor/destructor clones. The same elision policy applies: these functions are reconstruction metadata only, not buildable project modules.

| Address range | RawIDA hint | Decision |
| --- | --- | --- |
| `0x00442450`-`0x004426A0` | `sDataContainers.cpp` allocation/capacity checks | Elide into the concrete owning feature; no generic data-container build target. |
| `0x00442720`-`0x00442790` | `invalid string position` diagnostics | Preserve only at a real string/file-format boundary if a caller requires the exception behavior. |
| `0x00442820`-`0x00443030` | vector/string buffer helpers and destructors | Treat as STL adapter substrate; use `std::vector` / `std::string` directly in owner modules. |
| `0x004430A0`-`0x00443590` | string append/assign/compare and typed wrapper clones | Fold into existing config/resource parsers; do not expose a legacy string container. |
| `0x00443610`-`0x00443960` | `map/set<T> too long` and `list<T> too long` guards | Toolchain/STL guard semantics; mark as elided unless a real map/list feature boundary appears. |

## Final contiguous container sweep: `0x004439F0` through `0x004582D0`

This sweep closes the contiguous container-support island before non-container feature code resumes. It covers the remaining `sDataContainers.cpp` typed wrappers, list/map/set iterator guards, helper destructors, SEH cleanup thunks, `sUMapContainer.h` helpers, and the `sVectorContainer.cpp` dispatcher. Treat the whole island as support substrate: keep the addresses in `cut_functions_current`, but do not create buildable container modules.

| Address range | RawIDA hint | Decision |
| --- | --- | --- |
| `0x004439F0`-`0x00444FD0` | list/string/vector length checks and typed string helpers | Elide; preserve only feature-visible validation at parser/resource boundaries. |
| `0x004450C0`-`0x00446220` | `sDataContainers.cpp` wrappers plus map/set helpers | Elide; later owners use `std::map`, `std::set`, `std::vector`, or existing project lists directly. |
| `0x00446380`-`0x00447AE0` | allocator-backed vector/list helper clones | Elide; these are STL adapter mechanics and capacity guards. |
| `0x00447B70`-`0x0044D7B0` | map/set iterator and node-management families | Elide as container substrate unless a concrete owning feature proves domain semantics. |
| `0x0044D860`-`0x004582D0` | `sListContainer.cpp`, `sSetContainer.cpp`, `sVectorContainer.cpp`, and `sUMapContainer.h` cleanup/dispatch helpers | Finalize as non-build metadata; do not port as standalone containers. |
