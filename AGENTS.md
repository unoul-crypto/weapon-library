# Project conventions

- Communicate in Russian; keep source documentation, examples and diagnostics in English.
- Keep the C++17 core independent of graphics, physics engines, inventories and platform APIs.
- Separate immutable definitions, runtime state, commands and emitted events.
- Prefer small reusable mechanisms over speculative frameworks or inheritance trees.
- Preserve timing rules for zero-duration phases and updates crossing multiple phases.
- Run the headless behavioral tests after simulation changes; checks must work in Release.
- Keep generated builds and absolute local paths out of source files.
- Include the version number when creating a commit.
