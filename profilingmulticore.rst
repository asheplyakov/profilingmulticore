====================================
Profiling with modern multicore CPUs
====================================


Introduction
============

What could be possibly wrong with this program?

.. code:: C++

   void counterBumpThread(std::atomic<int> *cnt, unsigned N) {
       for (; N-- != 0;) { cnt->fetch_add(1); }
   }

   void run(unsigned tCount, unsigned N) {
       std::vector<std::atomic<int>> counters(tCount);
       for (auto& x: counters) { x.store(0); }
       std::vector<std::thread> threads;
       for (unsigned i = 0; i < tCount; i++) {
           threads.emplace_back(counterBumpThread, &counters[i], N);
       }
       for (auto& t: threads) { t.join(); }
   }


Expectations
------------

* Run time does not depend on number of threads if there are enough cores
* Run time grows linearly with the number of repetitions **N**

Reality
-------

With 4-core 8-thread CPU (Core i7-7700)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

[demo time]

With dual-socket 10-core CPUs (2 x Xeon E5-2687W)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

[demo time]


* The run time heavily depends on number of threads. Why?


Old good times
==============

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


Lost paradise
=============

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


Modern profiling techniques
===========================

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


Low-level tools
---------------

LIKWID_

.. _LIKWID: http://github.com/RRZE-HPC/likwid


Kernel (Linux)
--------------

* `perf_event`: abstraction of performance counters, both hardware
  (PMUs) and software (context switches, minor page faults, etc)
* `kprobes`, `uprobes`: dynamic tracing facilities



Sampling profiling
------------------

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


Dynamic tracing 
---------------

uprobes
~~~~~~~

* Probe associated with a file+offset
* Affects all processes that run code from that file
* Makes a special copy of the page to contain the probe 
* The instruction at the specified offset is replaced by a breakpoint
* When a breakpoint is hit by a process `filter` will be called
* Than `handler` runs unless `filter` says otherwise

Overhead: interrupt when a probe is hit by a process

kprobes
~~~~~~~

* Kernel debugging mechanism, which can be used for tracing too
* Similar to `uprobes`_ (but more compilcated)


High level tools
================

perf
----

Finding false sharing
~~~~~~~~~~~~~~~~~~~~~

.. code:: bash

   sudo perf stat -r 3 ./falsesharing -t1
   sudo perf stat -r 3 ./falsesharing

Instructions per cycle with 20 threads look too low. Why?

.. code:: bash

   sudo perf stat -r 3 cycles,instructions,cache-references,cache-misses,l1d.replacement ./falsesharing 


Side note: CPU cache
~~~~~~~~~~~~~~~~~~~~

Cache entry:

+-----+------------------------+--------+
| Tag | data block (cacheline) | flags  |
+=====+========================+========+
|     |                        |        |
+-----+------------------------+--------+

Memory address:

+-----+-------+--------------+
| Tag | index | block offset |
+=====+=======+==============+
|     |       |              |
+-----+-------+--------------+



Writing to a memory location requires exclusive access to the whole cache line.
Typical cache line size for x86 CPUs is 64 bytes


False sharing
~~~~~~~~~~~~~

Data located too closely in the memory can't be accessed in parallel (except
for read only), even if there is no actual sharing.

The problem is very common. It's especially serious with NUMA system.
And most of current multi-socket systems are NUMA (modern processors have
an integrated RAM controller, so each package is a NUMA node)

`perf` provides a tool for tracking down false sharing: `perf c2c`_

.. _perf c2c: http://man7.org/linux/man-pages/man1/perf-c2c.1.html

.. code:: bash

   sudo perf c2c record ./falsesharing
   sudo perf c2c report

Where exactly (and when) the problem occurs?

* Capture stack traces

.. code:: bash

   sudo perf c2c record --call-graph=fp ./falsesharing
   sudo perf script --header | gzip -9 > falsesharing.stacks.gz

* Make a `flame graph`_ or inspect the data with `flamescope`_

.. _flame graph: http://www.brendangregg.com/flamegraphs.html
.. _flamescope: https://github.com/Netflix/flamescope


Side note: stack unwinding
~~~~~~~~~~~~~~~~~~~~~~~~~~

Apparently figuring out the call chain is not such a simple task.
It requires availability of debugging information (so `perf` can find
the offset from the current stack pointer to the return address).

There are several ways to unwind stack:

#. frame pointers (`rbp` register on x86_64), typically disabled by compiler optimizations
#. `DWARF`_ (debugging with attributed record formats), complicated state machine

To use DWARF `perf` needs to copy the whole stack area (default: 32MB on x86_64)
to userspace (and do stack unwinding during `perf script` or `perf report`).
If the event driving the profiling happens frequently (CPU cycle, memory load/store,
etc) the data rate can be overwhelming (~ 100 -- 500 MB/sec). Such an overhead
is typically unacceptable.

On the other hand `perf` can unwind stack with frame pointers in the kernel
and copy less than 1KB (unless the call chain is longer than 128). However
there's an implicit overhead: the compiler can't use the `rbp` register for
other purposes, so the code with frame pointers enabled might be up to 15%
slower. Thus distributions compile software with frame pointers disabled,
hence a recompilation with `-fno-omit-frame-pointer` compiler option is required.

.. _DWARF: http://www.dwarfstd.org/doc/Debugging%20using%20DWARF-2012.pdf
