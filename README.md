Wyatt-STM
=========

This is a software transactional memory (STM) system developed at Wyatt Technology for use in their [Dynamics data collection and analysis package](http://www.wyatt.com/products/software/dynamics.html). This package includes the STM library itself (including some data structures built on top of the core STM functionality) and a suite of test programs to ensure its correct functioning. 

Contents
--------
The main STM library is in the `wstm` (headers) and `src` (implementation) directories.

In the `testing` directory there are some programs that exercise the library:

unit-tests: Unit tests for the STM library. This program uses the boost test library, for more information on command line arguments see that library's documentation.

correctness: Random stress test for the STM library. Makes random updates to a set of transactional variables in multiple threads pausing periodically to check that the library is behaving properly under load. This is meant to be run for long periods of time.

contention: Test how much contention is inherent in the STM system. This is done by running some number of threads with each thread updating its own private set of variables. The number of transactions that each thread can commit over some time period is tracked and reported. Since each thread is accessing variables that are not accessed by any other thread we won't have any contending transactions, all we're measuring is how much contention is inherent in the STM implementation itself.

channel: This is a stress test for the multi-cast channel data structure that is part of the library.

Documentation
-------------
The documentation for using the library is contained in the `doc` sub-directory.

Supported Platforms
-------------------
This library has been tested on Windows using Visual Studio 2015, Linux using GCC 4.9 and Clang 3.4, and OSX using Apple Clang 7.0. Earlier versions of these compilers probably won't work. It also requires the [boost](http:boost.org) library, version 1.57 or newer. For more details on building the library and associated test programs see `BUILDING.md`. 

Building
--------
See the `BUILDING.md` file for more information on building the library and associated test programs.

License
-------
This library and its associated test programs are licensed under a BSD license, see `LICENSE` for more information. 
