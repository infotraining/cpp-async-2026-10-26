# Intel baremetal senders/receivers demo (Linux)

[src/main_intel.cpp](src/main_intel.cpp) shows how the
[intel/cpp-baremetal-senders-and-receivers](https://github.com/intel/cpp-baremetal-senders-and-receivers)
library (namespace `async`) is used on a hosted Linux system. The interrupts a
microcontroller would raise are simulated with threads and file descriptors.

## Build and run

```bash
cmake -S . -B build
cmake --build build --target baremetal_sr_intel
./build/baremetal_sr_intel
```

CMake fetches the library (and its dependencies: `stdx`, `concurrency`, `mp11`, `fmt`)
with `FetchContent`. The target requires C++23. The output is deterministic:
`[UART] sent: ...` lines for each part, `[ERROR]` lines in part D and a `[STOP]` line in part E.

## Core idea

In the library, work is described as a *sender* pipeline (`then`, `when_all`,
`repeat_until`, `incite_on`, ...) and started with `start_detached()`. Nothing
blocks or allocates. A `trigger_scheduler<"name">` is a scheduler whose work
runs **when an interrupt handler calls `async::run_triggers<"name">()`**. So a
pipeline is:

1. *started* from the main context: its first step registers itself on a named trigger and returns;
2. *resumed* from inside the ISR that fires that trigger;
3. *continued* synchronously in that ISR until the next trigger-based step.

## Simulated interrupts

| Class | Simulates | Mechanism |
|---|---|---|
| `TimerIrq` | periodic hardware timer | `timerfd` (`CLOCK_MONOTONIC`) polled by a thread; the thread is the "ISR" and runs a handler on each expiry |
| `SoftIrq<T>` | pended, lower-priority interrupt with payload | `eventfd` (semaphore mode) + a mutex-protected queue of `T`; `raise(value)` can be called from any context and the ISR thread runs the handler |

Both use a second `eventfd` to wake and stop their thread on destruction. Each
handler only forwards to the library, e.g.
`[] { async::run_triggers<"timer_a">(); }`, exactly as a real ISR would.
`log()` serialises console output between threads.

The helpers `simulated_adc_read`, `simple_lowpass` and `simulated_uart_send`
stand in for hardware access; replace them with real drivers on a target.

## The demos

Parts A-C run 3 ticks (`ticks`), then signals a `Done` flag (an `atomic<bool>`
wait/notify) that lets the main thread continue. `repeat_until` re-arms the
pipeline after each tick and stops after the third.

### Part A - simple pipeline

```
trigger_scheduler<"timer_a"> -> then(adc) -> then(lowpass) -> then(uart) -> repeat_until
```

A single timer IRQ fires every 50 ms and the whole chain runs inside that ISR.

### Part B - `when_all`

```
when_all( trigger<"adc1_done"> -> adc1 ,  trigger<"adc2_done"> -> adc2 )
   -> then(r1 + r2) -> then(uart) -> repeat_until
```

Two timer IRQs fire 10 ms apart (ADC 2 starts at 60 ms). `when_all` waits for
both triggers, then delivers both values together; they are fused and sent.
It needs no buffering of its own: the partial result is held in the operation state.

### Part C - `incite_on`

```
trigger<"timer_c"> -> then(adc) -> incite_on(trigger<"proc_irq", int>) -> then(lowpass)
                    -> incite_on(trigger<"uart_irq", int>) -> then(uart) -> repeat_until
```

Each stage runs in a different interrupt context (a different thread). The
step before `incite_on` returns a function; `incite_on` first arms the target
trigger, *then* calls that function (here `SoftIrq::raise(value)`), which
makes the ISR fire. This avoids the race a plain `continue_on` would have, where
the interrupt could fire before the scheduler is ready. The `int` template
argument means `run_triggers<"proc_irq">(v)` passes `v` to the next step.

### Part D - error handling

```
trigger<"timer_d"> -> then(adc) -> let_value(make_variant_sender(ok, just(v), just_error(code)))
                    -> then(lowpass + uart) -> upon_error(log) -> repeat_until
```

Every second sample is deliberately out of range. Senders have three
completion channels (value, error, stopped), and adaptors only act on the
channel they care about:

- `let_value` picks at run time between `just(raw)` and `just_error(adc_error::out_of_range)`
  (`make_variant_sender` lets both branches have different sender types).
- An error skips the downstream `then` (the UART send never happens for a bad sample).
- `upon_error` handles it and completes on the *value* channel again, so `repeat_until`
  re-arms the pipeline. Without it the error would end the whole pipeline.

The library is freestanding-friendly, so errors are values on the error channel, not exceptions.

### Part E - cancellation with a stop token

```
(trigger<"timer_e"> -> then(adc) -> then(lowpass + uart) -> repeat)
    | stop_when(trigger<"cancel_e"> -> then(log))  -> then(done)
```

The pipeline is endless. `stop_when` races it against a second sender that
waits on the `cancel_e` trigger, which here is a one-shot `TimerIrq` (175 ms)
standing in for a button interrupt. When that ISR fires:

1. the cancel sender completes, which determines the completion of `stop_when`;
2. the pipeline is asked to stop through its stop token;
3. the waiting `timer_e` trigger operation is removed from the trigger queue and completes as *stopped*;
4. `repeat` starts no further iteration, and the final `then` signals `Done`.

Because the stop callback is attached to the trigger operation itself, no flags or polling are needed in the pipeline.

**Why not `start_detached_stoppable` + `request_stop()`?** That API returns a handle
with `request_stop()`, but calling it from the same thread that then waits hangs
here: the library runs the stop callbacks (and so the whole completion, including
`notify()`) inline while holding its concurrency-policy lock, which defaults to a
non-recursive `std::mutex` on Linux, so `notify()` deadlocks on it. Requesting
stop from an event/ISR through `stop_when` avoids this.

### Part F - environment queries

```
(trigger<"timer_f"> -> then(adc) -> let_value(raw -> read_env(get_gain, get_stop_token) -> then(send raw*gain))
    | repeat | upon_stopped(log))  | sync_wait_dynamic(env)
```

A receiver has an *environment*: a set of key/value pairs looked up with
*queries* (`get_stop_token`, `get_scheduler`, `get_allocator`, ...). Adaptors
forward the environment of their receiver to the senders they wrap, so any
sender in the pipeline can read it with `read_env(query...)` instead of
receiving the values as arguments.

- The environment is built with `async::env{ async::prop{query, value}, ... }` and handed to the consumer (`sync_wait_dynamic(sndr, env)`).
- It contains the standard `get_stop_token` query (an `inplace_stop_source` owned by `part_env`) and a **custom** query, `get_gain` (a calibration factor). A custom query is just a tag type deriving from `async::forwarding_query_t` (so that it is forwarded through adaptors) with an `operator()` that calls `env.query(tag)`.
- `read_env(get_gain, get_stop_token)` sends both values downstream; the stage prints them (`gain=3 stop_possible=1 ...`). Without an environment the stop token would be `never_stop_token` and `stop_possible()` false.
- The same token is used by the library itself: the pending `timer_f` trigger operation registers a callback on it. When the cancel ISR calls `stop_src.request_stop()`, the trigger operation completes as *stopped*, `repeat` ends, and `upon_stopped` converts that into a value so `sync_wait_dynamic` (which needs a value completion and blocks the main thread in a run loop) returns.

Differences from part E: here the stop source is owned by the caller and supplied through the environment, so no `stop_when` and no second sender is needed; `start_detached*` is not used because it installs its own stop token over the one in the environment.

## Notes

- Pipelines are started with a small `start()` helper that keeps the `[[nodiscard]]` result of `start_detached()` alive in a local.
- The library's `then` accepts lambdas, not plain function references, so free functions are wrapped.
- The `Done`/atomic wait is the only blocking in the program; all pipeline work happens in the simulated ISRs.
- On real hardware the `TimerIrq`/`SoftIrq` classes would be replaced by actual interrupt vectors that call `run_triggers`. Thread-safety then relies on the library's concurrency policy (`conc::injected_policy`), which on a bare-metal target would typically disable interrupts around critical sections.
- The original hand-written simulation is kept in [src/main.cpp](src/main.cpp) (target `baremetal_sr_linux`).
