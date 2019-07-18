# Off-CPU profiling

## Why my threads are blocked?

[Off-CPU analysis](http://www.brendangregg.com/offcpuanalysis.html):
measuring and studying off-CPU time along with context such as stack traces.

Two types of performance problems:

* On-CPU: threads spend (too many) time running on CPU
* Off-CPU: thread(s) spend (too many) time while blocked on locks, IO, paging, etc

Method: tracing the scheduler (all blocking code paths in system calls end
up in the scheduler)

### Wakeup stacks

Off-CPU stack shows the blocking path, but not the full reason it got blocked
(i.e. who was holding the lock). That reason is often with another thread,
the one that called a wakeup on a blocked thread (example: a web server with
a thread pool doing network IO).

#### Wakeup flame graphs

[thunderingherd wakeup stacks](./img/thunderingherd_wakestacks.svg)

### Off-CPU profiling in Linux

* [bcc-tools](https://github.com/iovisor/bcc)
  * offwaketime
  * offcputime
* [FlameGraph](https://github.com/brendangregg/FlameGraph)

##### Prerequisite

The application must be compiled with frame pointers (`-fno-omit-frame-pointer`)

##### Recording a trace

**Beware: on a busy system tracing the scheduler for a long time can make the system totally unresponsive**

Launch the program, and trace its offcpu time (and wakeup stacks) for 10 seconds 

```bash
./bin/thunderingherd &
pid=$!
sudo /usr/share/bcc/tools/offwaketime -f -p $pid 10 -M 999999999 10 > thunderingherd_wakestacks.txt
```

Make a flame graph

```bash
./tools/FlameGraph/flamegraph.pl --title 'Thundering herd' --colors wakeup --countname usec thunderingherd_wakestacks.txt > thunderingherd_wakestacks.svg
```

## Summary

* Often people care only about CPU cycles spent by the app
* Time spent by threads when they are **not** running on CPUs is also important
* Wakeup stacks are useful for finding **why** a thread was blocked
* Off-CPU profiling might incur substantial overhead, especially with workloads
  with a heavy context switching (200kps)
* Flame graphs are useful for off-CPU analysis

