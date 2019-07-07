=============================================
Tracking down cache line contention with perf
=============================================

Introduction
============

What could be possibly wrong with this program?

.. code:: C++

   void counterBumpThread(std::atomic<int>* cnt, unsigned N) {
       for (; N-- != 0;) { cnt->fetch_add(1); }
   }

   void run(unsigned tCount, unsigned N) {
       std::vector<std::atomic<int>> counters(tCount);
       for (auto& x: counters) { x.store(0); }
       std::vector<std::thread> threads;
       for (auto& x: counters) {
           threads.emplace_back(counterBumpThread, &x, N);
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

There are enough cores, but the run time heavily depends on number of threads.


Memory caches
=============

The memory of multicore CPUs is a hierarchical network

.. image:: memorytopo_2x_xeon.png
   :alt: dual-socket system memory topology


How caches store data
---------------------

.. image:: Cache_Fill.svg
   :alt: direct fill and two-way caches

Memory address

+-----+--------+--------------+
| Tag | index  | block offset |
+=====+========+==============+
|     | [13:6] |    [5:0]     |
+-----+--------+--------------+

Cache entry

+-----+------------------------+--------+
| Tag | data block (cacheline) | flags  |
+=====+========================+========+
|     |    64 bytes of data    |        |
+-----+------------------------+--------+



