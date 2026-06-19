# Threading and host tuning

The shard runtime (`include/lob/shard_runtime.hpp`) runs one worker thread
per shard, each pinned to a core, each draining a dedicated SPSC ingress
ring into its engine. Production-quality, low-jitter latency depends as
much on host configuration as on the code. This page records the host
setup the latency numbers assume.

## Runtime placement

`shard_runtime_config` controls where workers land and how they wait.

```cpp
lob::shard_runtime_config cfg{
    .pin_threads = true,   // pin each worker to a core
    .first_core  = 4,      // worker 0 -> core 4
    .core_stride = 1,      // worker i -> core (first_core + i * stride)
    .spin_budget = 1024,   // cpu_relax spins before yielding
};
```

Worker `i` pins to `first_core + i * core_stride`. A stride of one packs
workers onto contiguous cores. A stride of two skips SMT siblings so each
worker owns a full physical core, which is usually what a latency build
wants. Choose `first_core` to avoid core 0, since the kernel steers most
housekeeping and timer work there.

`spin_budget` bounds the busy-wait. On a dedicated isolated core a large
budget keeps wake latency near zero. On a shared development host a small
budget returns the core to other work sooner. Pinning is best effort, so a
host that ignores the hint, or any non-Linux and non-macOS target, runs the
workers unpinned and the runtime still functions.

## Linux

Isolate the worker cores from the scheduler, the timer tick, and RCU
callbacks, and steer interrupts away from them.

- Kernel command line for cores 4 to 7.

  ```
  isolcpus=4-7 nohz_full=4-7 rcu_nocbs=4-7 irqaffinity=0-3
  ```

  `isolcpus` keeps the general scheduler off the worker cores,
  `nohz_full` stops the periodic timer tick on cores running a single
  task, `rcu_nocbs` offloads RCU callbacks to the housekeeping cores, and
  `irqaffinity` confines interrupt handling to cores 0 to 3.

- Hold every core at a fixed high frequency so a frequency transition
  never stalls the hot path.

  ```bash
  sudo cpupower frequency-set -g performance
  ```

- Stop transparent huge pages from collapsing under the engine at an
  unpredictable moment. The arena requests explicit huge pages instead.

  ```bash
  echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
  ```

- Pull network-card interrupts off the worker cores so feed traffic does
  not preempt a shard. Set `/proc/irq/<n>/smp_affinity` for each device
  queue to the housekeeping mask.

Verify a worker landed where intended.

```bash
ps -T -p <pid> -o tid,comm,psr | grep lob-shard
```

The `psr` column is the core the thread last ran on. A correctly pinned
build shows each `lob-shard-NN` thread on a distinct isolated core that
never changes.

## macOS

macOS exposes no hard CPU pin. `thread_policy_set` with an affinity tag is
a hint that asks threads sharing a non-zero tag to prefer the same L2, and
the scheduler may still migrate them. Use macOS for development and
correctness work; capture production latency numbers on a tuned Linux host.

## Egress

`shard_runtime` shares one publisher across every worker, so that publisher
must be thread safe. `shard_egress_runtime` removes that requirement by
giving each shard its own SPSC egress ring fed by a `ring_publisher`. Worker
i is the sole producer of egress ring i and a single downstream consumer
drains it with `try_poll`, so the publish path holds no lock and contends no
cache line across cores. A full ring drops the event and bumps a per-shard
loss counter; size the ring to the worst-case burst and drain it promptly.

When a downstream wants one feed rather than a per-shard poll loop,
`egress_merger` runs a single thread that owns the consumer side of every
egress ring and forwards events to one sink stamped with a gap-free global
sequence. Quiesce the producing runtime before stopping the merger so its
final pass drains every ring.

## Verifying correctness under threading

`tests/test_shard_runtime.cpp` and `tests/test_shard_egress_runtime.cpp`
assert each runtime reproduces the synchronous router's book state byte for
byte over a randomised command stream, and `tests/test_egress_merger.cpp`
asserts the merger delivers every event exactly once in a gap-free sequence.
Run the suite under ThreadSanitizer on Linux to check the memory ordering of
the ingress and egress rings, the stop flag, and the drain counters.

```bash
cmake --preset linux-clang-tsan
cmake --build --preset linux-clang-tsan --target lob_tests --parallel
ctest --preset linux-clang-tsan -R "runtime|egress|merger"
```
