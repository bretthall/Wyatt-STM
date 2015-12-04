/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

//Stress test for WChannel and associated classes, a given number of
//readers and writers are run on one channel.

#include "channel.h"
using namespace WSTM;

#include <boost/format.hpp>
using boost::format;
using boost::str;
#include <boost/program_options.hpp>

#include <iostream>
#include <thread>
#include <random>

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
   auto s_mt = std::mt19937 (std::random_device ()());
}

bool ShouldExit (std::uniform_int_distribution<unsigned int>& dist)
{
   return (dist (s_mt) == 0);
}

void DecrementCount (WVar<unsigned int>& count_v, WAtomic& at)
{
   const int oldCount = count_v.Get (at);
   assert (oldCount > 0);
   if (oldCount > 0)
   {
      count_v.Set (oldCount - 1, at);
   }
}

auto MakeReaderFactory (std::uniform_int_distribution<unsigned int>& exitDist, WChannelReader<int> reader, WVar<unsigned int>& numReaders_v, WVar<bool>& done_v)
{
   return [&exitDist, reader, &numReaders_v, &done_v]() mutable
   {
      Atomically ([&](WAtomic& at){numReaders_v.Set (numReaders_v.Get (at) + 1, at);});
      
      return std::thread ([&exitDist, reader, &numReaders_v, &done_v]() mutable
                          {
                             auto DoRead = [lastVal = -1, &exitDist, reader, &numReaders_v, &done_v](WAtomic& at) mutable
                                {
                                   if (done_v.Get (at) || ShouldExit (exitDist))
                                   {
                                      DecrementCount (numReaders_v, at);
                                      return false;
                                   }

                                   const auto val_o = reader.ReadRetry (at);
                                   assert (*val_o > lastVal);
                                   at.After ([&lastVal, val = *val_o](){lastVal = val;});
                                   return true;
                                };
                             while (Atomically ([&](WAtomic& at){return DoRead (at);}))
                             {}
                          });
   };
}

auto MakeWriterFactory (std::uniform_int_distribution<unsigned int>& exitDist,
                        WVar<unsigned int>& nextVal_v,
                        WChannel<int>& chan,
                        WVar<unsigned int>& numWriters_v,
                        WVar<bool>& done_v)
{
   return [&exitDist, &nextVal_v, &chan, &numWriters_v, &done_v]
   {
      Atomically ([&](WAtomic& at){numWriters_v.Set (numWriters_v.Get (at) + 1, at);});
      
      return std::thread ([&exitDist, &nextVal_v, &chan, &numWriters_v, &done_v]() mutable
                          {
                             auto DoWrite = [writer=WChannelWriter<int>(chan), &exitDist, &nextVal_v, &numWriters_v, &done_v](WAtomic& at) mutable
                                {
                                   if (done_v.Get (at) || ShouldExit (exitDist))
                                   {
                                      DecrementCount (numWriters_v, at);
                                      return false;
                                   }

                                   const auto nextVal = nextVal_v.Get (at);
                                   writer.Write (nextVal, at);
                                   nextVal_v.Set (nextVal + 1, at);
                                   return true;          
                                };
                             while (Atomically ([&](WAtomic& at){return DoWrite (at);}))
                             {}
                          });
   };
}

namespace
{
   const auto defaultExitChance = 1000u;
   const auto defaultDuration = 10u;
}

int main (int argc, const char** argv)
{
   const auto numHWThreads = std::thread::hardware_concurrency ();

   auto numReaders = 0u;
   auto numWriters = 0u;
   auto exitChance = 0u;
   auto durationSecs = 0u;
   namespace po = boost::program_options;
   po::options_description desc;
   desc.add_options ()
      ("help", "Display help message")
      ("readers,R", po::value<unsigned int>(&numReaders)->default_value (numHWThreads/2), "The number of reader threads to run")
      ("writers,W", po::value<unsigned int>(&numWriters)->default_value (numHWThreads/2), "The number of writer threads to run")
      ("exitChance,X",
       po::value<unsigned int>(&exitChance)->default_value (defaultExitChance),
       "Thread exit chance denominator, e.g. 1 in <exitChance> chance of exiting on each loop iteration")
      ("duration,D", po::value<unsigned int>(&durationSecs)->default_value (defaultDuration), "How long to run for in seconds");
   po::variables_map vm;
   po::store(po::parse_command_line(argc, argv, desc), vm);
   po::notify(vm);
   if (vm.count("help"))
   {
      std::cout << desc << std::endl;
      return 1;
   }

   std::cout
      << str (format ("Running %1% readers and %2% writers with a 1 in %3% chance of exitting each iteration for %4% seconds")
              % numReaders % numWriters % exitChance % durationSecs)
      << std::endl;
   
   std::unique_ptr<WChannel<int>> chan_p (new WChannel<int>);
   WVar<unsigned int> nextVal_v (0);
   WVar<unsigned int> numReaders_v (0);
   WVar<unsigned int> numWriters_v (0);
   std::deque<std::thread> threads;
   WVar<bool> done_v (false);
   auto exitDist = std::uniform_int_distribution<unsigned int>(0, exitChance);

   auto UpdateReaderWriters = [&](WAtomic& at)
      {
         bool retry = true;
   
         const auto curReaders = numReaders_v.Get (at);
         if (curReaders < numReaders)
         {
            retry = false;
            at.After ([&, num = numReaders - curReaders]
                      {
                         std::cout << "Starting " << num << " readers" << std::endl;                   
                         std::generate_n (std::back_inserter (threads), num, MakeReaderFactory (exitDist, *chan_p, numReaders_v, done_v));
                      });
         }

         const auto curWriters = numWriters_v.Get (at);
         if (curWriters < numWriters)
         {
            retry = false;
            at.After ([&, num = numWriters - curWriters]
                      {
                         std::cout << "Starting " << num << " writers" << std::endl;                   
                         std::generate_n (std::back_inserter (threads), num, MakeWriterFactory (exitDist, nextVal_v, *chan_p, numWriters_v, done_v));
                      });
         }
   
         if (retry)
         {
            Retry (at, std::chrono::seconds (1));
         }
      };

   const auto duration = std::chrono::seconds (durationSecs);
   const auto start = std::chrono::steady_clock::now ();
   while ((std::chrono::steady_clock::now () - start) < duration)
   {
      try
      {
         Atomically (UpdateReaderWriters);
      }
      catch(WRetryTimeoutException&)
      {
         //ignore, just retrying so that timer can be checked
      }
   }
   
   done_v.Set (true);
   std::cout << "Waiting for thread exits" << std::endl;
   for (auto& t: threads)
   {
      t.join ();
   }

#ifdef WATCH_MEMORY
   chan_p.reset ();
   std::cout << "Remaining nodes = " << Internal::GetNumNodes () << std::endl;
   std::cout << "Max node num = " << Internal::GetMaxNodeNum () << std::endl;
#endif //WATCH_MEMORY

   return 0;
}
