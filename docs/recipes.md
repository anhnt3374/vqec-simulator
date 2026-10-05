# Recipes

Step-by-step checklists for the most common changes. Each ends with the definition of done from
`AGENTS.md` section 9: builds with `cpu-debug` without warnings, `ctest` passes,
`scripts/check.sh` passes, docs and examples updated.

## Add a solution (bài toán)

1. Create `src/application/solutions/<type>/<type>_solution.{hpp,cpp}`:
   - `struct <Type>Params` with defaults; `class <Type>Solution final : public ISolution`.
   - `static constexpr std::string_view kType = "<type>";` and one constant per event type
     `"<type>.<event>"`. Attribute keys as constants too.
   - Follow the shared track conventions (Tentative ignored, Lost keeps state, absent forgotten).
   - Log scene changes at `debug`; never log per frame above `trace`.
2. Add the `.cpp` to `src/application/CMakeLists.txt`.
3. Register it in `src/bootstrap/solution_factory.cpp` (`create_solution` + `available_solution_types`):
   parse params from YAML, validate referenced zone/gate ids.
4. Tests in `tests/unit/application/<type>_solution_test.cpp`: enter/leave, Lost/Tentative handling,
   label filter, scene change. Use `tests/support/builders.hpp`.
5. Add an example block to `config/simulator.example.yaml` (the config test loads it).
6. Document the event types and attributes in `docs/rest_api.md` (Events section).

If the solution needs data the pipeline does not provide (embeddings, attributes, dwell time
from timestamps), extend `FrameContext` or add an outbound port — do not call adapters.

## Add an inference backend / runtime (e.g. an NPU)

1. Create `src/adapters/outbound/inference/runtimes/<backend>/` with `<backend>_engine.{hpp,cpp}`
   implementing `IInferenceEngine` and a `CMakeLists.txt` defining target
   `vqec_inference_<backend>` (links `vqec_inference_engine` + the vendor SDK).
2. Add `option(VQEC_WITH_<BACKEND> ...)` to `cmake/vqec_options.cmake`, the `add_subdirectory`
   to `src/adapters/CMakeLists.txt`, the flag to `src/bootstrap/build_info.hpp.in` and the
   `foreach` list in `src/bootstrap/CMakeLists.txt`.
3. Add the backend name to `kBackends` in `src/bootstrap/app_config.cpp` and the engine
   construction in bootstrap wiring (`#if VQEC_WITH_<BACKEND>` lives only there).
4. Docker: for a new runtime class add `devbase-<rt>`, `dev-<rt>`, `runbase-<rt>`, `runtime-<rt>`
   stages to `docker/Dockerfile` with pinned SDK versions; add `<rt>-debug/<rt>-release` presets.
5. Add per-backend artifacts to `models/manifest.yaml` (compiled/quantized model files).
6. Logging: on `load()` log backend, SDK version, device, model, precision and load time at
   `info`; log every failure with the vendor error code.
7. Integration test under `tests/integration/inference/<backend>/` labelled `integration`,
   skipped with a message when the device is absent. Never claim it works without running it on
   the hardware.

## Add a model

1. Add an entry to `models/manifest.yaml` (id with version, task, labels, input spec,
   artifacts per backend with sha256). Do not commit the binary.
2. If it is a new model family, add a codec in `src/adapters/outbound/inference/models/<family>/`
   implementing the relevant port (`IObjectDetector`, `IFeatureExtractor`) on top of
   `IInferenceEngine`. Unit-test pre/post-processing with small synthetic tensors.

## Add an adapter for an existing port

1. Directory `src/adapters/<inbound|outbound>/<family>/<technology>/`, files prefixed by the
   technology (`sqlite_event_repository.cpp`), namespace `vqec::adapters::<technology>`.
2. Own CMake target + `VQEC_WITH_*` option; link only `vqec_ports`, `vqec_log` and its libraries.
3. Honour the port contract written in the port header (blocking, thread-safety, error codes).
   Convert library errors/exceptions to `Error` at the boundary.
4. Log connect/open/close at `info`, retries at `warn`, failures at `error`, with a context tag.
5. Integration test labelled `integration`. Enable the option in the presets.

## Wire a new adapter in bootstrap

Bootstrap creates shared services first (detector, repositories, vector store, publisher), then
one `StreamPipeline` per stream on its own `std::thread`, starts the HTTP server, waits for
SIGINT/SIGTERM, requests stop, and joins threads in reverse order. All `#if VQEC_WITH_*`
conditionals live in bootstrap factory functions such as `create_inference_engine(config)`.

## Add a REST endpoint

1. Define it in `docs/rest_api.md` first (path, method, request/response JSON, errors).
2. If it needs new use-case behavior, add/extend an inbound port in `src/ports/inbound/` and
   implement it in `src/application/services/`.
3. Implement the handler in `src/adapters/inbound/http/`; map `ErrorCode` → HTTP status as in
   `docs/rest_api.md`. Log failures; access line at `debug`.
4. Call it from the WebUI only through `web/js/api_client.js`.

## Change the database schema

1. Add `db/migrations/NNNN_<description>.sql` (next number). Never edit an applied migration.
2. Use the portable SQL subset (see the header of `0001_initial_schema.sql`).
3. Update the repository adapters and their integration tests in the same change.
