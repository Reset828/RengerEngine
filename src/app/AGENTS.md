# src/app

## OVERVIEW
Qt 5 application shell; translates user actions into engine commands and presents snapshots/events.

## TARGET
- CMake target: `dzc_app` executable.
- Always links `Qt5::Widgets`; adds `Qt5::OpenGL` and `OpenGLRenderWidget.cpp` when `DZC_ENABLE_OPENGL` is enabled.
- Private include roots: `src` and `src/app`; backend factories arrive through composition.

## FILE MAP
- `main.cpp`: process entry, settings/CLI load, backend composition, window startup.
- `CommandLineOptions.*`: strict option parsing into `AppConfigOverrides`/`EngineConfig`.
- `SettingsController.*`: standard per-user INI load/save and CLI overlay.
- `ApplicationComposition.h`: render/compute factory assembly; reports degraded compute.
- `EngineUiAdapter.*`: Qt-facing port for commands, lifecycle, snapshots, and events.
- `MainWindow.*`: menus, docks, dataset controls, render parameters, log/status refresh.
- `OpenGLRenderWidget.*`: context-bound init/update/render/resize and frame scheduling.
- `StatusPresenter.*`, `LogPanelModel.*`: pure-ish formatting/model helpers for presentation.

## UI FLOW
- Parse CLI -> load settings -> apply explicit overrides -> compose backends.
- Initialize adapter/engine before wiring a live render widget.
- Widget `initializeGL` owns context-sensitive backend init; `paintGL` drives update/render.
- Qt input handlers become `InputEvent` commands through `EngineUiAdapter`.
- `refreshEngineState()` polls one snapshot/event batch and updates docks/logs/status.
- Dataset open/cancel, view reset, colors, shading, point size, CUDA mode all enqueue commands.

## CONVENTIONS
- Keep `MainWindow` widget ownership in its Pimpl; use object names expected by tests/UI lookup.
- Revert a control visually when an adapter command fails; surface `Error` in status and log.
- Use `QSignalBlocker` while synchronizing controls from engine/config state.
- Treat `StatusPresenter::format` as non-widget formatting; do not retain snapshots there.
- Keep render-widget shutdown on the current graphics context.

## ANTI-PATTERNS (THIS DIRECTORY)
- Do not put engine state machines, dataset mutation, or worker coordination in slots/widgets.
- Do not call engine internals directly when an `EngineUiAdapter` command exists.
- Do not initialize OpenGL resources outside `initializeGL`/the active context lifecycle.
- Do not assume CUDA creation succeeds; preserve `computeDegraded` and present capability state.
- Do not save a control value before the corresponding engine command succeeds.
- Do not accept arbitrary dataset extensions; the file dialog and registry contract is `.pcd`/`.ply`.
- Do not run blocking point-cloud reads on the Qt GUI thread.
