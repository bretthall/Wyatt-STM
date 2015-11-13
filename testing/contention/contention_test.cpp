/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

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
      v.Set (0);
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
                        std::invoke (f, v, at);
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
   const auto doSet = vm.count ("set");
   
   std::cout << "Running " << (doSet ? "set" : "get") << " operations in " << numThreads << " threads for " << durationSecs
             << " seconds with " << numVars << " vars in each transaction" << std::endl;
   
   boost::barrier bar (numThreads);

   auto threads = std::vector<std::thread>();
   if (!doSet)
   {
      for (auto i = size_t (0); i < numThreads; ++i)
      {
         threads.push_back (std::thread ([&]() {RunTest (&WVar<int>::Get, bar, numVars);}));
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
