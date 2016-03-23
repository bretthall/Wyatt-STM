// Copyright (c) 2015, Wyatt Technology Corporation
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:

// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//Tests how many non-conflicting transactions we can commit per second.

#include "stm.h"
using namespace WSTM;

#include <boost/timer/timer.hpp>
#include <boost/range/numeric.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/program_options.hpp>

#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

#ifdef NON_APPLE_CLANG 
//Clang on linux is missing this
extern "C" int __cxa_thread_atexit(void (*func)(), void *obj, void *dso_symbol)
{
   int __cxa_thread_atexit_impl(void (*)(), void *, void *);
   return __cxa_thread_atexit_impl(func, obj, dso_symbol);
}
#endif //NON_APPLE_CLANG 

namespace
{
   std::atomic<bool> keepRunning (true);
   
   std::mutex resultsMutex;
   auto results = std::vector<boost::timer::nanosecond_type>();

   const auto ns_per_s = boost::timer::nanosecond_type (1000000000);
}

template <typename F_t>
void RunTest (const F_t& f, boost::barrier& bar, const size_t numVars)
{
   auto vars = std::vector<WVar<int>>(numVars);
   for (auto& v: vars)
   {
      SetVar (v, 0);
   }

   auto count = size_t (0);

   bar.wait ();
   
   auto timer = boost::timer::cpu_timer ();
   do
   {
      Atomically ([&](WAtomic& at)
                  {
                     for (auto& v: vars)
                     {
                        f (v, at);
                     }
                  });
      ++count;
   }while (keepRunning.load ());

   const auto elapsedSecs = timer.elapsed ().wall/ns_per_s;
   std::lock_guard<std::mutex> lock (resultsMutex);
   results.push_back (count/elapsedSecs);
}

int main (int argc, const char** argv)
{
   auto numThreads = 0u;
   auto numVars = 0u;
   auto durationSecs = 0u;
   namespace po = boost::program_options;
   po::options_description desc;
   desc.add_options ()
      ("help", "Display help message")
      ("version", "The program and library version")
      ("set,S", "Change variable values instead of just reading them")
      ("threads,T", po::value<unsigned int>(&numThreads)->default_value (1), "The number of threads to run")
      ("vars,V", po::value<unsigned int>(&numVars)->default_value (1), "The number of vars to use in each thread")
      ("duration,D", po::value<unsigned int>(&durationSecs)->default_value (10), "How long to run for in seconds");
   po::variables_map vm;
   po::store(po::parse_command_line(argc, argv, desc), vm);
   po::notify(vm);
   if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 1;
   }
   if (vm.count ("version"))
   {
      const auto version = GetVersion ();
      std::cout << "Version = " << version.m_major << "." << version.m_minor << "." << version.m_patch << std::endl;
   }
   const auto doSet = vm.count ("set");
   
   std::cout << "Running " << (doSet ? "set" : "get") << " operations in " << numThreads << " threads for " << durationSecs
             << " seconds with " << numVars << " vars in each transaction" << std::endl;
   
   boost::barrier bar (numThreads);

   auto threads = std::vector<std::thread>();
   if (!doSet)
   {
      for (auto i = size_t (0); i < numThreads; ++i)
      {
         threads.push_back (std::thread ([&]() {RunTest ([](WVar<int>& v, WAtomic& at) {return v.Get (at);}, bar, numVars);}));
      }
   }
   else
   {
      const auto DoSet = [](auto& var, auto& at) {var.Set (var.Get (at) + 1, at);};
      for (auto i = size_t (0); i < numThreads; ++i)
      {
         threads.push_back (std::thread ([&]() {RunTest (DoSet, bar, numVars);}));
      }
   }

   boost::this_thread::sleep_for (boost::chrono::seconds (durationSecs));
   keepRunning.store (false);
   for (auto& t: threads)
   {
      t.join ();
   }

   std::lock_guard<std::mutex> lock (resultsMutex);
   const auto avg = boost::accumulate (results, 0.0)/numThreads;
   std::cout << "Transactions/second = " << avg << std::endl;
   
   return 0;
}
