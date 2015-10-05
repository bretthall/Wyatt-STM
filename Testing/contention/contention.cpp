/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

//Tests how many non-conflicting transactions we can commit per second.

#include "BSS/Thread/STM/stm.h"
using namespace  bss::thread::STM;

#include <boost/foreach.hpp>
#include <boost/timer/timer.hpp>
#include <boost/range/numeric.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/atomic.hpp>
#include <boost/chrono/duration.hpp>

#include <iostream>
#include <vector>

namespace
{
   boost::atomic<bool> keepRunning (true);
   
   boost::mutex resultsMutex;
   auto results = std::vector<boost::timer::nanosecond_type>();

   const auto ns_per_s = boost::timer::nanosecond_type (1000000000);
}

void RunTest (boost::barrier& bar, const boost::timer::nanosecond_type time, const size_t numVars)
{
   auto vars = std::vector<WVar<int>>(numVars);
   BOOST_FOREACH (auto& v, vars)
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
                     BOOST_FOREACH (auto& v, vars)
                     {
                        //change below to select read-only versus read-write transactions
                        v.Get (at);
                        //v.Set (v.Get (at) + 1, at);
                     }
                  });
      ++count;
   }while (keepRunning.load ());

   const auto elapsedSecs = timer.elapsed ().wall/ns_per_s;
   boost::lock_guard<boost::mutex> lock (resultsMutex);
   results.push_back (count/elapsedSecs);
}

int main (int argc, const char** argv)
{
   if (argc != 4)
   {
      std::cout << "Usage: stm_contenction_test <num threads> <time to run for (seconds)> <num vars>" << std::endl;
      return 1;
   }

   const auto numThreads = boost::lexical_cast<size_t>(argv[1]);
   const auto time = boost::lexical_cast<boost::timer::nanosecond_type>(argv[2]);
   const auto numVars = boost::lexical_cast<size_t>(argv[3]);

   std::cout << "Running " << numThreads << " threads for " << time
             << " seconds with " << numVars << " vars in each transaction" << std::endl;
   
   boost::barrier bar (numThreads);

   auto threads = std::vector<boost::thread>();
   for (auto i = size_t (0); i < numThreads; ++i)
   {
      threads.push_back (boost::thread ([&](){RunTest (bar, time, numVars);}));
   }
   boost::this_thread::sleep_for (boost::chrono::seconds (time));
   keepRunning.store (false);
   BOOST_FOREACH (auto& t, threads)
   {
      t.join ();
   }

   boost::lock_guard<boost::mutex> lock (resultsMutex);
   const auto avg = boost::accumulate (results, 0.0)/numThreads;
   std::cout << "Transactions/second = " << avg << std::endl;
   
   return 0;
}
