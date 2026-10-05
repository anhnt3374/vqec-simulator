# Coding style

The short version is in `AGENTS.md` section 7. This document explains the reasoning and shows
examples. Everything that a tool can enforce is enforced by a tool:

| Concern | Tool | Config |
| --- | --- | --- |
| Formatting | clang-format 14 | `.clang-format` |
| Naming, bug patterns, modernization | clang-tidy 14 | `.clang-tidy`, `tests/.clang-tidy` |
| Layer dependencies | `scripts/check_layers.sh` | rules in the script header |
| Warnings | GCC 11 | `cmake/vqec_warnings.cmake` (`-Werror`) |
| Whitespace in non-C++ files | EditorConfig | `.editorconfig` |

Guiding principle: **code is read far more than written** — optimize for the next person (or
agent) who has to change it without context.

## 1. Naming

| Kind | Style | Example |
| --- | --- | --- |
| Files, directories | `snake_case` | `zone_intrusion_solution.hpp` |
| Namespaces | `snake_case` | `vqec::domain` |
| Classes, structs, enums, aliases | `PascalCase` | `StreamPipeline`, `BoxF`, `TrackId` |
| Interfaces (pure abstract classes) | `I` + `PascalCase` | `IFrameSource`, `ISolution` |
| Functions, methods | `snake_case`, verb first | `detect_crossing()`, `apply_scene()` |
| Variables, parameters, public fields | `snake_case` | `frame_width`, `track.label` |
| Private/protected data members | `snake_case_` | `last_anchors_` |
| Constants (`constexpr`, `static const`) | `k` + `PascalCase` | `kMinVertices`, `kEnterEvent` |
| Enumerators (`enum class` only) | `PascalCase` | `TrackState::Confirmed` |
| Macros (avoid; only `VQEC_LOG_*` exist) | `VQEC_UPPER_CASE` | `VQEC_LOG_INFO` |
| Template parameters | `PascalCase` | `Item`, `T` |

Rules of thumb:
- Names say *what*, not *how*: `select_zones()`, not `filter_vector_by_id()`.
- Booleans read as predicates: `is_live`, `has_image`, `should_reconnect`.
- Units go in the name when the type does not carry them: `stride_bytes`, `stats_interval_s` in
  config, `max_file_size_mb`. In C++ prefer `std::chrono` types over unit suffixes.
- No abbreviations except well-known ones (`id`, `uri`, `fps`, `rtsp`, `nms`, `iou`).
- Namespaces: one per layer — `vqec` (common), `vqec::domain`, `vqec::ports`, `vqec::app`,
  `vqec::adapters::<adapter>`, `vqec::bootstrap`. Sub-folders do not add namespaces.

### Files

- One primary type per header, file named after it: `StreamPipeline` → `stream_pipeline.hpp`.
- Port headers drop the `I`: `IFrameSource` → `ports/outbound/frame_source.hpp`.
- Adapter files are prefixed by the technology: `ffmpeg_frame_source.cpp`,
  `sqlite_event_repository.cpp`, `tensorrt_engine.cpp`.
- Tests: `<file>_test.cpp` under `tests/unit/` mirroring `src/`.

## 2. Formatting

Never format by hand; run `scripts/dev.sh scripts/format.sh`. The resulting style:
4-space indent, 100 columns, braces on the same line, `int* pointer` (left-aligned), always
braces on `if`/`for`/`while` bodies, short functions inline only inside class bodies.

Includes are grouped and sorted automatically:

```cpp
#include "application/pipeline/stream_pipeline.hpp"  // 1. own header (in .cpp files)

#include <memory>                                    // 2. C++ standard library
#include <vector>

#include <spdlog/spdlog.h>                           // 3. third-party (adapters/bootstrap only)

#include "common/log.hpp"                            // 4. project, always from src/
#include "domain/scene/gate.hpp"
```

## 3. Headers

```cpp
#pragma once

#include <string>

#include "domain/ids.hpp"

namespace vqec::domain {

// One-paragraph contract: what it is, units, ownership, thread-safety.
struct Gate {
    GateId id;
    std::string name;
    Segment line;  // Normalized [0, 1] frame coordinates.
};

}  // namespace vqec::domain
```

- `#pragma once`, then includes, then one namespace block closed with a comment.
- Include what you use; never rely on transitive includes.
- No `using namespace` in headers. In `.cpp` files only inside functions or for `std::chrono_literals`.
- Keep implementation out of headers unless it is a template or a trivial accessor.

## 4. Classes and functions

```cpp
// Raises "zone_intrusion.enter" when a confirmed track's anchor moves into a zone.
class ZoneIntrusionSolution final : public ISolution {
public:
    static constexpr std::string_view kType = "zone_intrusion";

    explicit ZoneIntrusionSolution(ZoneIntrusionParams params);

    std::string_view type() const override { return kType; }
    std::vector<domain::Event> process(const FrameContext& context) override;

private:
    ZoneIntrusionParams params_;
    std::set<Membership> inside_;
};
```

- Order inside a class: `public` → `protected` → `private`; inside each: types, constants,
  constructors/destructor, methods, data members.
- `explicit` on single-argument constructors, `final` on leaf classes, `override` (never
  `virtual` together with `override`). Interfaces have a `virtual` defaulted destructor and only
  pure virtual methods.
- Resource-owning classes follow the rule of zero; if not possible, rule of five, with copy and
  move explicitly deleted or defaulted.
- `struct` = passive data, all public, no invariants. `class` = has invariants, private data.
  Types with invariants get a validating factory: `Result<Zone> make_zone(...)`,
  `Result<Polygon> Polygon::create(...)`.
- Functions: one job, ≤ ~60 lines, ≤ 4–5 parameters (otherwise a params struct), cognitive
  complexity ≤ 25 (clang-tidy enforces). Early returns over nesting.
- Parameters: cheap types (`int`, `float`, `Point2f`, `std::string_view`) by value; others by
  `const&`; sink parameters (stored) by value + `std::move`. Out-parameters only for hot-path
  buffer reuse, and documented.
- `const` everything that does not change. `auto` when the type is obvious from the right-hand
  side (`make_unique`, casts, iterators), explicit type otherwise.
- `enum class` only. `switch` over an enum lists every enumerator and has no `default:` so the
  compiler flags new values.
- No C-style casts; `static_cast` for numeric conversions (warnings require it).

## 5. Errors

```cpp
Result<Gate> make_gate(GateId id, std::string name, Point2f start, Point2f end) {
    if (!is_normalized(start) || !is_normalized(end)) {
        return make_error(ErrorCode::InvalidArgument,
                          "gate '" + id + "' has endpoints outside normalized [0, 1] coordinates");
    }
    return Gate{std::move(id), std::move(name), Segment{start, end}};
}

// Caller decides and logs once.
if (Status status = sink->write(frame, annotations); !status.ok()) {
    VQEC_LOG_ERROR("[stream={}] sink failed: {} ({})", stream_id_, status.error().message,
                   to_string(status.error().code));
    return status;
}
```

- Messages are lowercase, specific, and name the entity: `"zone 'door' has 2 vertices"`, not
  `"invalid input"`.
- Exceptions from third-party code (yaml-cpp, spdlog, Drogon, ORT C++ API) are caught inside the
  adapter/bootstrap translation unit and converted to `Error`.

## 6. Comments

- Document contracts in headers: ownership, thread-safety, blocking, units, coordinate space,
  error codes. That is what the next reader cannot see from the signature.
- In `.cpp` files, comment *why* (a non-obvious decision, a workaround with a link), not *what*.
- No commented-out code, no `TODO` without an issue/owner: `// TODO(#42): batch across streams`.
- English only in code and comments.

## 7. Modern C++ checklist (C++17)

Use: `std::optional`, `std::variant`, `std::string_view` (parameters only), structured
bindings, `if (init; cond)`, `[[nodiscard]]`, `constexpr`, `std::filesystem`, `<chrono>`.
Avoid: raw `new`/`delete`, owning raw pointers, `std::endl` (use `'\n'`), `std::bind`,
`#define` constants, `NULL`, C arrays (use `std::array`), `std::shared_ptr` where `unique_ptr`
suffices, `mutable` except for mutexes and caches.

## 8. Web (HTML/CSS/JS)

- Plain HTML, CSS and ES modules; no framework or bundler unless an ADR approves it.
- 2-space indent, semicolons, single quotes, `const`/`let` (never `var`), `camelCase`
  identifiers, `UPPER_SNAKE` module constants, `snake_case.js` file names.
- All backend calls go through `web/js/api_client.js`; no `fetch` elsewhere.
- No inline scripts or `on*=` attributes. Colors are CSS variables in `:root`.
- Coordinates sent to the backend are normalized `[0, 1]`.

## 9. Shell scripts and CMake

- Bash with `set -euo pipefail`, quoted variables, `readonly` for constants, a usage comment at the
  top. Scripts must work when run from any directory.
- CMake: lowercase commands, one target per adapter, `target_*` commands only (never global
  `include_directories`/`link_libraries`), project warnings via `vqec_set_warnings()`.
