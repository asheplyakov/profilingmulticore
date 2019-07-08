# Underpinning of Linux profiling tools: PMU, uprobes, eBPF, and all that

## Introduction

### Old good times

* A single CPU
* CPU executes instructions sequentially in the program order
* An instruction always takes the same number of cycles
* Relatively small number of instructions (RISC)
* OS is essentially a library

Optimizing code boils down to

* doing the same job with less CPU cycles
* more efficient/clever use of registers

The only question is to find which functions worth optimizing.

* Tell the compiler to instrument every call/return
* Run the program with realistic inputs
* Measure the call count (using instrumented call/ret) of every function CC(f)
* Measure (or calculate) the cost of a single call (in cycles) of every function CY(f)
* Calculate cycles spent per a function: Cost(f) = CC(f) \* CY(f)
* Sort functions by their cost, pick top 5 -- 10 of them for the optimization


### Lost paradise

* Massively multicore CPUs

* Virtual memory (hardware enforced separation between OS and "user" programs)

* Memory is essentially a hierarchical network (L1/L2/L3 caches, NUMA)

* Implicit parallelism: superscalar execution, pipelining, speculative execution

Consequences:
 

* Just because a function takes more cycles doesn't mean it takes more time
  (a parallel algorithm can outperform a sequential one)

* Memory operation might require up to 3 extra memory references if the virtual
  to the physical address mapping is not in TLB yet

* Implicit parallelism: superscalar execution, pipelining, speculative execution

* The number of cycles it takes to execute an instruction depends on CPU state

  - Is the memory operand already in cache?
  - Has the previous instruction caused a pipeline stall?
  - Is virtual to physical address mapping in the TLB?

* System call throws away TLB, cache, pipeline state

* The ability of CPU to schedule instructions for speculative and superscalar
  execution depends on the compexity of the code and memory access patterns


Thus instrumentation increases the run time of a program in an unpredictable manner

* Updating a function call count might break a nice cache-friendly memory access pattern
* Updating a function call count might require synchronization (atomic instruction or
  even a system call) which reduces both the implicit and the explicit parallelism

Attempt to observe the system with a high accuracy heavily disturbs
its state (somewhat similar to quantum mechanics)


## Modern profiling techniques

* Hardware assisted sampling profiling
* Dynamic tracing (software based)


Hardware
--------

PMU (performance monitoring unit)

* A set of metrics maintained by hardware (cache misses, mispredicted branches, TLB misses)

* PEBS (precise event based sampling) counters

  - Can raise an interrupt every N events (cycles, instructions, cache misses, etc)

* LBR (last branch records)

  - Stores last 4 -- 16 branch locations in MSRs
  - Low overhead
  - Only in recent CPUs (Intel: Haswell and newer)

* BTS (branch trace store)

  - Uses DRAM to store instructions and events
  - High overhead (20% -- 100% or even more)

* AET (architectural event trace)

  - More selective tracing than LBR or BTS (interrupts, exceptions, breakpoints)
  - Data can be accessed via XDP (extended debug port)
  - Huge overhead if all events are captured

Highly CPU specific (radically different even between Intel CPUs)


#### Low-level tools

[LIKWID](http://github.com/RRZE-HPC/likwid)


#### Kernel (Linux)


* `perf_event`: abstraction of performance counters, both hardware
  (PMUs) and software (context switches, minor page faults, etc)
* `kprobes`, `uprobes`: dynamic tracing facilities


#### Sampling profiling

Profiler: 

* Asks kernel to sample stack of every process every N events (cycles)
* Provides dynamic instrumentation (eBPF program) to handle the data, or
  asks the kernel to dump the raw data to a circular buffer

Kernel: 

* Defines PMU interrupt handler
* Programs PEBS to raise interrupt every N events (cycles) 
* Handles PMU interrupt and 
* On PMU interrupt kernel either runs the instrumentation code or gives control to the profiler
* Profiler inspects the process(es) state and records the data (event count, stack trace, etc)

Profiler:

* Tells the kernel to stop profiling
* Either asks the results of the dynamic instrumentation program,
  or post-processes recorded data

Kernel:

* Tells PEBS to stop
* Passes the result of dynamic instrumentation program to the profiler


#### Dynamic tracing 

##### uprobes

* Probe associated with a file+offset
* Affects all processes that run code from that file
* Makes a special copy of the page to contain the probe 
* The instruction at the specified offset is replaced by a breakpoint
* When a breakpoint is hit by a process `filter` will be called
* Than `handler` runs unless `filter` says otherwise

Overhead: interrupt when a probe is hit by a process

##### kprobes

* Kernel debugging mechanism, which can be used for tracing too
* Similar to `uprobes`_ (but more compilcated)
