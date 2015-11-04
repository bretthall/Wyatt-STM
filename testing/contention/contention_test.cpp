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
   if (argc != 5)
   {
      std::cout << "Usage: stm_contenction_test <get/set> <num threads> <time to run for (seconds)> <num vars>" << std::endl;
      return 1;
   }

   const auto doGet = (std::string (argv[1]) == "get");
   const auto numThreads = boost::lexical_cast<size_t>(argv[2]);
   const auto time = boost::lexical_cast<boost::timer::nanosecond_type>(argv[3]);
   const auto numVars = boost::lexical_cast<size_t>(argv[4]);

   std::cout << "Running " << argv[1] << " operations in " << numThreads << " threads for " << time
             << " seconds with " << numVars << " vars in each transaction" << std::endl;
   
   boost::barrier bar (numThreads);

   auto threads = std::vector<std::thread>();
   if (doGet)
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

   boost::this_thread::sleep_for (boost::chrono::seconds (time));
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
