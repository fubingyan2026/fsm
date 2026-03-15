# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Build Commands
- Default build (Release): `cmake -B build && cmake --build build`
- Debug build: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
- Disable specific features: `cmake -B build -DFSM_ENABLE_EVENT_QUEUE=0 && cmake --build build`
- Available options: FSM_ENABLE_DEBUG, FSM_ENABLE_STATS, FSM_ENABLE_CALLBACKS, FSM_ENABLE_EVENT_QUEUE, FSM_ENABLE_ASSERT, FSM_ENABLE_HSM, FSM_ENABLE_TIMEOUT, FSM_ENABLE_TRACE

## Critical Framework Notes
- Event queue functions (`fsm_post_event`, `fsm_get_current_event`) require `FSM_ENABLE_EVENT_QUEUE=1` (default). Without it, they return `FSM_EVENT_NONE`.
- HSM (`FSM_ENABLE_HSM=1`) changes transition lookup: if no transition in current state, searches parent chain. When disabled (`0`), behaves as flat FSM.
- Timeout feature requires setting tick function via `fsm_set_tick_fn()` and adding timeouts with `fsm_add_timeout()`.
- Callbacks (`FSM_ENABLE_CALLBACKS`) must be enabled to use `fsm_set_callbacks()`; otherwise, the function is unavailable.
- Debug names (`FSM_ENABLE_DEBUG`) required for `fsm_set_state_names()` and `fsm_get_state_name()` to return meaningful strings.
- Statistics (`FSM_ENABLE_STATS`) must be enabled to use `fsm_get_stats()` and `fsm_clear_stats()`.
- Trace (`FSM_ENABLE_TRACE`) must be enabled to use `fsm_get_trace()` and `fsm_clear_trace()`.
- Assertions (`FSM_ENABLE_ASSERT`) affect `FSM_ASSERT` macro behavior; when disabled, only returns error code without `FSM_BREAK()`.

## Code Style Observations
- Indentation: 2 spaces (observed in source files)
- Function pointers use `typedef` for readability (e.g., `fsm_handler_t`)
- Error codes follow pattern `FSM_ERROR_*` with descriptive names
- Volatile used for variables that may change asynchronously (e.g., `current_state`, `state_changed`)
- Static assertions enforce constraints at compile time (e.g., queue sizes power of 2)
- Feature gates via `#if` blocks; disabled features omit code entirely (zero overhead when not used)

## Key Usage Patterns
- User data pattern: define struct, pass to `fsm_init()`, retrieve via `fsm_get_user_data()`
- Event-driven transitions: use condition functions (e.g., `is_emergency_mode`) or `FSM_COND_ALWAYS`
- State entry/exit callbacks: reset timers and log state changes (see `traffic_light_on_entry/exit`)
- Timeout setup: prevents state from being stuck indefinitely (see red/green/yellow timeouts in example)
- HSM parent relationships must be acyclic - circular parent chains will be detected and rejected
- The `FSM_COND_ALWAYS` sentinel is globally unique - do not attempt to create local copies
- State handler functions must return the next state or current state - returning invalid state IDs will cause `FSM_ERROR_INVALID_STATE`
- When using condition functions for transitions, they must be static or global - local function pointers will cause undefined behavior