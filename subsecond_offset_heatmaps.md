# [Subsecond offset heatmaps](http://www.brendangregg.com/HeatMaps/subsecondoffset.html)

An improved version of a CPU load graph

* x axis: time, seconds
* y axis: subsecond offset (milliseconds)
* color: number of events

Useful for

* Tracking down concurrency problems: lock contention, thundering herd, etc
* Understanding the behavior of the app (IO bound vs CPU bound)


