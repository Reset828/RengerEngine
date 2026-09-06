# Test Suite Guide

## OVERVIEW
- Tests are registered from the root CMake file only when `DZC_BUILD_TESTS=ON`.
- Root adds `unit`, `integration`, `performance`, and `ui` suites.
- Root adds `graphics` only when `DZC_ENABLE_OPENGL=ON`.
- Root also registers `dzc_target_boundary` configure-manifest validation.
- This directory has no umbrella `CMakeLists.txt`; edit the owning suite CMake file.

## Suite boundaries
- `unit/`: deterministic component and public-contract tests.
- `integration/`: data-reader, task, visibility, and optional render-pipeline flows.
- `graphics/`: fake-operation OpenGL contracts and optional real-context probes.
- `performance/`: performance-oriented executables.
- `ui/`: Qt host/widget behavior.
- `fakes/`: reusable test doubles such as `FakeCameraController`.
- `cmake/TargetBoundaryCheck.cmake`: architectural target policy, not runtime behavior.

## Test style
- Tests are standalone executable sources with `main()` and `<cassert>`.
- Name executable targets `dzc_<area>_tests`.
- Register the runnable CTest name without the `_tests` suffix.
- Set `cxx_std_17` on each executable.
- Include `src/` privately only when the test exercises private/internal headers.
- Add implementation `.cpp` files explicitly when a target does not supply them.
- Use fakes to observe calls, arguments, ordering, failures, and ownership.
- Make tests independent: use temporary files/directories for filesystem inputs.
- Keep tests deterministic; do not rely on machine timing or ambient GPU behavior.

## Result and error assertions
- Assert `hasValue()` before extracting a successful value.
- For negative paths, assert error domain/code and relevant diagnostic contract.
- Cover invalid input, invalid lifecycle/state, recovery, and repeated/idempotent calls.
- Preserve `noexcept` destruction and move-ownership tests for resource types.

## Target-boundary invariants
- Required foundation targets are checked through generated manifest data.
- Only `dzc_data_pcl` may link PCL; it must directly link `pcl_io`.
- Only `dzc_app` may link Qt Widgets.
- Backend/library targets must not depend on `dzc_app`.
- Required dependency edges in the boundary script are architectural contracts.

## OpenGL test convention
- Fake-context tests are the standard deterministic coverage.
- Real-context variants pass `--real-context`.
- Unavailable real context must return `77`, allowing CTest skip behavior.
- Keep labels specific: `graphics;opengl;glNNN`, plus `skipped` for real-context probes.

## ANTI-PATTERNS
- Do not add CMake targets in this directory; use the appropriate child suite.
- Do not couple unit tests to PCL, Qt Widgets, or a live GL context without a suite reason.
- Do not let a fake silently succeed when the test needs to observe failure propagation.
- Do not weaken target-boundary checks to accommodate a forbidden dependency.
- Do not turn an optional hardware probe into a mandatory CI/runtime requirement.
