Wyatt-STM
=========

This is a software transactional memory (STM) system developed at Wyatt Technology for use in their [Dynamics data collection and analysis package](http://www.wyatt.com/products/software/dynamics.html). This package includes the STM library itself (including some data structures built on top of the core STM functionality) and a suite of test programs to ensure its correct functioning. 

Contents
--------
The main STM library is in the `wstm` (headers) and `src` (implementation) directories.

In the `testing` directory 

Documentation
-------------
The documentation for using the library is contained in the `doc` sub-directory.

Supported Platforms
-------------------
This library has been tested on Windows using Visual Studio 2015, linux using GCC 4.9 and Clang 3.4, and OSX using Apple's Clang 7.0. Earlier versions of these compilers probably won't work. It also requires the [boost](http:boost.org) library, version 1.57 or newer. For more details on building the library and associated test programs see BUILDING.md. 

Building
--------
See the `BUILDING.md` file for more information on building the library and associated test programs.

License
-------
This library and its associated test programs are licensed under a BSD license, see `LICENSE` for more information. 
