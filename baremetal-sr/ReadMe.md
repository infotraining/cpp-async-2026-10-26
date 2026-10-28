
---

### How this maps to the original baremetal examples

- **`TriggerScheduler`** simulates `async::trigger_scheduler` by letting you register a callback and calling `trigger()` (like an ISR would).  
- **Part A** executes the pipeline inline when the scheduler is triggered (similar to running in ISR context).  
- **Part B** demonstrates `when_all` semantics by sampling both sensors on the same tick and fusing results. On baremetal, `when_all` may need bounded buffering; here we do synchronous reads so buffering is not required.  
- **Part C** shows `incite_on` semantics by enqueuing work to worker queues and notifying worker threads — this simulates moving work to lower‑priority contexts (deferred handlers or different ISRs).

---

### Practical tips for porting to Linux or embedded Linux

- **Real hardware access**: replace `simulated_adc_read` and `simulated_uart_send` with platform APIs (e.g., `read()` on device nodes, `libgpiod`, or vendor SDKs).  
- **Thread priorities**: on Linux you can set thread scheduling policies (`SCHED_FIFO`, `SCHED_RR`) and priorities for closer ISR priority simulation (requires root).  
- **Real-time constraints**: use `clock_nanosleep` with `CLOCK_MONOTONIC` and `TIMER_ABSTIME` for precise timers.  
- **Signal safety**: if you use real signals, avoid heavy work in signal handlers; instead use `eventfd` or `signalfd` to wake worker threads.  
- **Determinism**: for strict real‑time behavior prefer a real‑time kernel or an RTOS on embedded targets.

---

### Intel baremetal senders/receivers version (`baremetal_sr_intel`)

[src/main_intel.cpp](src/main_intel.cpp) uses [intel/cpp-baremetal-senders-and-receivers](https://github.com/intel/cpp-baremetal-senders-and-receivers) (fetched by CMake `FetchContent`).

- Interrupts are simulated: `timerfd` + thread for timer IRQs, `eventfd` + thread for software IRQs. Each "ISR" only calls `async::run_triggers<"name">(...)`.
- Part A: `trigger_scheduler` -> `then` chain -> `repeat_until`.
- Part B: `when_all` joining two ADC-done triggers fired at different offsets.
- Part C: `incite_on` migrates stages to the `proc_irq` and `uart_irq` triggers.
- Part D: error handling (`let_value` + `just_error`, `upon_error`).
- Part E: cancellation (`stop_when` racing the pipeline against a cancel interrupt).
- Part F: environment queries (`env`, `prop`, custom query, `read_env`, `sync_wait_dynamic`).

See [intel-snd-rcv.md](intel-snd-rcv.md) for details.

Build/run: `cmake -S . -B build && cmake --build build --target baremetal_sr_intel && ./build/baremetal_sr_intel`
