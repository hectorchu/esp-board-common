# FreeRTOS SMP: xTaskCreatePinnedToCore rejects -1 for core affinity

## Problem
`xTaskCreatePinnedToCore(..., -1)` crashes with an assertion failure on ESP-IDF v5.5.1 (FreeRTOS SMP port). The task never starts — it panics immediately in the `xTaskCreatePinnedToCore` wrapper before executing any user code.

## Symptoms
```
assert failed: xTaskCreatePinnedToCore freertos_tasks_c_additions.h:163
(( ( ( ( BaseType_t ) xCoreID ) >= 0 && ( ( BaseType_t ) xCoreID ) < 2 ) ...
```

The assertion is `taskVALID_CORE_ID(xCoreID)` which checks `xCoreID >= 0 && xCoreID < configNUM_CORES`. The value `-1` fails the `>= 0` check.

## Root cause
In older ESP-IDF (pre-SMP FreeRTOS), passing `-1` as the core ID to `xTaskCreatePinnedToCore` was the conventional way to say "no core affinity." The ESP-IDF v5.x FreeRTOS SMP port changed the validation — `xTaskCreatePinnedToCore` now asserts that the core ID is a valid core index (0 to `configNUM_CORES - 1`). The "no affinity" case is handled by `xTaskCreate()` instead.

## Fix
Replace:
```c
xTaskCreatePinnedToCore(task_fn, "name", stack, param, priority, &handle, -1);
```
With:
```c
xTaskCreate(task_fn, "name", stack, param, priority, &handle);
```

Or if you need to pin to a specific core, use `0` or `1` as the core ID.

## Debugging note
This crash is particularly sneaky because `xTaskCreatePinnedToCore` returns `pdPASS` to the caller (the assertion fires inside the wrapper, not via the return value), and the task function's first log line never executes. It looks like the task was created successfully but silently died — when in reality it panicked during creation. The core dump or serial output shows the real cause.

## Affected platforms
Observed on ESP32-P4 with ESP-IDF v5.5.1. Likely affects all dual-core ESP32 variants using the FreeRTOS SMP port (ESP32, ESP32-S3, ESP32-P4).
