# Threading and host tuning

The shard runtime (`include/lob/shard_runtime.hpp`) runs one worker thread per
shard, each pinned to a core, each draining a dedicated SPSC ingress ring into
its engine. Production-quality, low-jitter latency depends as much on host
configuration as on the code. This page records the host setup the latency
numbers assume.

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

Worker `i` pins to `first_core + i * core_stride`. A stride of one packs workers
onto contiguous cores, and a stride of two skips SMT siblings to give each
worker a full physical core, the usual choice for a latency build. Choose
`first_core` to avoid core 0, since the kernel steers most housekeeping and
timer work there.

`spin_budget` bounds the busy-wait. On a dedicated isolated core a large budget
keeps wake latency near zero. On a shared development host a small budget
returns the core to other work sooner. Pinning is best effort, so a host that
ignores the hint, or any non-Linux and non-macOS target, runs the workers
unpinned and the runtime still functions.

## Linux

Isolate the worker cores from the scheduler, the timer tick, and RCU callbacks,
and steer interrupts away from them.

- Kernel command line for cores 4 to 7.

  ```text
  isolcpus=4-7 nohz_full=4-7 rcu_nocbs=4-7 irqaffinity=0-3
  ```

  `isolcpus` removes the worker cores from the general scheduler, `nohz_full`
  stops the periodic timer tick on cores running one task, `rcu_nocbs` offloads
  RCU callbacks to the housekeeping cores, and `irqaffinity` confines interrupt
  handling to cores 0 to 3.

- Set every core to a fixed high frequency so a frequency transition never
  stalls the hot path.

  ```bash
  sudo cpupower frequency-set -g performance
  ```

- Stop transparent huge pages from collapsing under the engine at an
  unpredictable moment. The arena requests explicit huge pages instead.

  ```bash
  echo madvise | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
  ```

- Reserve 2 MiB huge pages so the slab arena backs its storage with them (see
  ADR-0023). Size the pool to the engine's arenas plus headroom.

  ```bash
  echo 512 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
  cat /proc/meminfo | grep -i hugepages_total
  ```

  The arena falls back to regular pages when none are reserved, so this is a
  latency optimisation, not a correctness requirement.

- Pull network-card interrupts off the worker cores so feed traffic does not
  preempt a shard. Set `/proc/irq/<n>/smp_affinity` for each device queue to the
  housekeeping mask.

Verify a worker landed where intended.

```bash
ps -T -p <pid> -o tid,comm,psr | grep lob-shard
```

The `psr` column is the core the thread last ran on. A correctly pinned build
shows each `lob-shard-NN` thread on a distinct isolated core that never changes.

## macOS

macOS doesn't expose a hard CPU pin. `thread_policy_set` with an affinity tag is
a hint that asks threads sharing a non-zero tag to prefer the same L2, and the
scheduler may still migrate them. Use macOS for development and correctness
work. Measure production latency on a tuned Linux host.

## Egress

`shard_runtime` shares one publisher across every worker, so that publisher must
be thread safe. `shard_egress_runtime` removes that requirement by giving each
shard its own SPSC egress ring fed by a `ring_publisher`. With worker i as the
only producer of egress ring i and one downstream consumer draining it with
`try_poll`, the publish path doesn't take a lock or contend for a cache line
across cores. A full ring drops the event and bumps a per-shard loss counter;
size the ring to the worst-case burst and drain it promptly.

For a downstream that reads one feed rather than a per-shard poll loop,
`egress_merger` runs one thread as the consumer of every egress ring and
forwards events to one sink stamped with a gap-free global sequence. Each round
claims at most `merger_config::batch_max` events per shard in one ring cursor
claim, so a backlogged shard cannot stall the shards behind it and the merged
stream interleaves by round. Quiesce the producing runtime before stopping the
merger so its final pass drains every ring.

Each shard's engine seeds its event sequence from `lob::shard_seq_base`, so the
engine seq stamps stay globally unique after the per-shard streams merge; the
merger's own merge sequence is a separate gap-free counter over the forwarded
order.

## Verifying correctness under threading

`tests/test_shard_runtime.cpp` and `tests/test_shard_egress_runtime.cpp` assert
each runtime reproduces the synchronous router's book state byte for byte over a
randomised command stream, and `tests/test_egress_merger.cpp` asserts the merger
delivers every event exactly once in a gap-free sequence. Run the suite under
ThreadSanitizer on Linux to check memory ordering on the ingress and egress
rings. The same run covers the stop flag and the drain counters.

```bash
conan install . -s:a compiler.cppstd=20 --build=missing -s build_type=Debug -o "&:sanitizer=thread"
cmake --preset conan-sanitizer_thread-debug
cmake --build --preset conan-sanitizer_thread-debug --target lob_tests
ctest --preset conan-sanitizer_thread-debug -R "runtime|egress|merger"
```
