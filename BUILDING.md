Building Wyatt-STM
==================

Wyatt-STM uses [CMake](https://cmake.org/) to generate the build systems used on various platforms. Please consult the CMake documentation for information on generating build files for your platform. In what follows we will give some platform specific tips that have worked in the past.

Prerequisites
-------------
The only external dependency of Wyatt-STM is [boost](http://boost.org). Version 1.57 or later is required (prior to 1.57 `boost::optional` lacked a move constructor).

Build Directories
-----------------
It is recommended that you create a sub-directory under the source tree root and run cmake from within it. This will isolate your platform specific build artifacts from the rest of the tree and if you're using unix makefiles it will allow you to create debug and release configurations (a separate build directory for each).

For example, you could do this at unix style command line starting in the root of the source tree in order to create the makefiles and then run make with them:

   bash$ mkdir debug
   bash$ cd debug
   bash$ cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
   bash$ make
   
Windows
-------
Wyatt-STM makes extensive use of C++11 and C++14 features so the minimum version of Visual Studio that can compile it is 2015. Be sure to specify the `Visual Studio 14 2015` generator to cmake. You will probably also need to give the `BOOST_ROOT` cmake variable the correct value in order for cmake to find your boost libraries.

Linux (GCC)
-----------
GCC 4.9 or later is required, just use the `Unix Makefiles` generator (should be the default). If boost is not installed in the usual location you will probably need to specify a value for `BOOST_ROOT` when running cmake. Note that you need to specify `CMAKE_BUILD_TYPE` as either `Debug` or `Release`, if you don't specify it things will not work correctly.

Linux (Clang)
-------------
Clang 3.4 is known to work, earlier versions may work but no guarantees are being made. Again use the `Unix Makefiles` generator but this time specify the `CMAKE_C_COMPILER` as the path to `clang` and `CMAKE_CXX_COMPILER` as the path to `clang++`. If boost is not installed in the usual location you will probably need to specify a value for `BOOST_ROOT` when running cmake. Note that you need to specify `CMAKE_BUILD_TYPE` as either `Debug` or `Release`, if you don't specify it things will not work correctly.

OSX
---
Apple Clang 7.0 is known to work, earlier versions may work too but no guarantees are being made. You can use either the `Unix Makefiles` or `XCode` generators. If you use makefiles then you will need to specify `CMAKE_BUILD_TYPE` as either `Debug` or `Release` when running cmake, if you don't specify it things will not work correctly.
