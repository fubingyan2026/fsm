# Copilot Instructions (workspace guidance)

This repository is a small C-based finite-state-machine (FSM) library + example.

## 🧩 Project at a glance
- **Primary library:** `fsm.c` / `fsm.h`
- **Example executable:** `fsm_example.c` (built as `fsm_example`)
- **Build system:** CMake

## ✅ Common tasks (what agents should do)
- Use `cmake` to build and run the example (`./build/fsm_example`).
- Make changes in `fsm.c`/`fsm.h` for core FSM behavior.
- Update `fsm_example.c` for usage examples and testing new API behavior.
- Keep code consistent with the existing style (2-space indentation, clear naming).

## 🛠 Build & run
```sh
cmake -B build && cmake --build build
./build/fsm_example
```

### Optional build flags
CMake defines optional compiler macros for feature gating. Example:
```sh
cmake -B build -DFSM_ENABLE_EVENT_QUEUE=0 && cmake --build build
```
See `AGENTS.md` for the full list.

## 📄 Existing workspace guidance
- `AGENTS.md` contains the current build options, feature notes, and important framework behaviors.

## 🧠 Agent behavior guidelines
- Prefer small, safe changes; verify builds succeed after modifications.
- If adding APIs or behavior, keep the public interface (`fsm.h`) backward compatible.
- When in doubt, read `fsm.h` comments; they are the authoritative API docs.

---

*Created/maintained to help AI agents (Copilot) be productive in this repo.*
