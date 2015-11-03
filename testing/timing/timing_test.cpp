/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

#include <BSS/Thread/STM/stm.h>
using namespace  bss::thread::STM;

#include "TSS/Logger/Logging.h"

#include <boost/timer.hpp>
#include <boost/thread/thread.hpp>
using boost::thread;
using boost::thread_group;
#include <boost/thread/barrier.hpp>
using boost::barrier;
#include <boost/ref.hpp>
using boost::ref;
#include <boost/date_time/posix_time/posix_time.hpp>
using namespace boost::posix_time;
#include <boost/foreach.hpp>
#include <boost/format.hpp>
using boost::format;
using boost::str;

#include <windows.h>
#undef min
#undef max

#include <iostream>
#include <algorithm>

//This program runs multiple threads that simply increment a counter
//stored in a STM::WVar. The incrementing is timed and reported every
//so often. This tests either the raw performance of the STM system if
//one thread is used or how much contention impacts the STM
//performance when multiple threads are used.

//Amount of time threads are run for before results are extracted
const time_duration ITERATION_TIME = seconds (30);
//the number of threads to run.
const size_t NUM_THREADS = 2;
//The number of values in the value array
const size_t NUM_VALUES = 10;

//Current Intel processors have 64 byte cache lines. Need to pad
//certain structures that are shared between threads to avoid false
//sharing when these structures are put in arrays with adjacent
//entries accessed by different threads. The padding needs to be
//larged enough so that the structure fills a cache line.
const size_t CACHE_LINE_LENGTH = 64;

logging::WLogger& logger = logging::getLogger ("StmTiming");

void LogMemAllocation (const char* type, const void* address, const char* filename, const int line)
{
   LOG_DEBUG (logger, str (format ("%1% allocated at %2% (%3%:%4%)")
                           % type % address % filename % line));
}

#define LOG_MEM_ALLOCATION(type, address) \
   LogMemAllocation (#type, address, __FILE__, __LINE__)

void LogMemDeallocation (const char* type, const void* address, const char* filename, const int line)
{
   LOG_DEBUG (logger, str (format ("%1% deallocated at %2% (%3%:%4%)")
                           % type % address % filename % line));
}

#define LOG_MEM_DEALLOCATION(type, address)                \
   LogMemDeallocation (#type, address, __FILE__, __LINE__)

struct WThreadResult
{
   double m_elapsed;
   unsigned int m_numTransactions;
   char m_cacheLinePadding[CACHE_LINE_LENGTH
                           - sizeof(double)
                           - sizeof(unsigned int)];
   
   WThreadResult () :
      m_elapsed (0.0),
      m_numTransactions (0)
   {}
};

struct WThreadInfo
{
   size_t m_index;
   WThreadResult m_results[NUM_THREADS];

   WThreadInfo (const size_t index):
      m_index (index)
   {}
};

typedef std::vector<WVar<int>::Ptr> ValueVec;
typedef boost::shared_ptr<ValueVec> ValueVecPtr;

void Read (const ValueVec& values, WAtomic& at)
{
   BOOST_FOREACH (const WVar<int>::Ptr& value_p, values)
   {
      value_p->Get (at);
   }
}

void Increment (ValueVec& values, WAtomic& at)
{
   BOOST_FOREACH (WVar<int>::Ptr& value_p, values)
   {
      const int old = value_p->Get (at);
      value_p->Set (old + 1, at);
   }
}

enum ThreadType
{
   READ,
   WRITE
};
   
void TestThread (const size_t threadNum,
                 const ThreadType type,
                 volatile WThreadInfo*& infoIn_p,
                 volatile LONG* infoIndex,
                 const ValueVecPtr& values_p,
                 boost::barrier& bar)
{
   WThreadInfo* info_p = 0;
   boost::timer timer;
   bar.wait ();
   
   while (true)
   {
      if (info_p != infoIn_p)
      {
         info_p = (WThreadInfo*)infoIn_p;
         InterlockedExchange (infoIndex, info_p->m_index);
         timer.restart ();
      }

      if (type == READ)
      {
         Atomically (bind (Read, ref (*values_p), _1));
      }
      else
      {
         Atomically (bind (Increment, ref (*values_p), _1));
      }
      
      info_p->m_results[threadNum].m_elapsed = timer.elapsed ();
      ++info_p->m_results[threadNum].m_numTransactions;
   }
}

struct WThreadInfoIndex
{
   volatile LONG m_index;
   char m_cacheLinePadding[CACHE_LINE_LENGTH - sizeof(size_t)];

   WThreadInfoIndex ():
      m_index (0)
   {}
};

void After ()
{
   //does nothing
}

int ExtractInt (WVar<int>& v, const bool doSet, const bool doGet, WAtomic& at)
{
   // const int val = v.Get (at);
   // v.Set (val + 1, at);
   // //at.TestSetFlag ("sdfljsdlkf");
   // //at.After (After);
   // return val;

   static int sval = 0;
   
   if (doSet && doGet)
   {
      const int val = v.Get (at);
      v.Set (val + 1, at);
      return val;
   }
   else if (doSet)
   {
      ++sval;
      v.Set (sval, at);
      return sval;
   }
   else if (doGet)
   {
      const int val = v.Get (at);
      return val;
   }
   else
   {
      return 0;
   }
}

boost::mutex s_outputMutex;

void ExtractionThread (const int threadNum, WVar<int>& v, const bool doSet, const bool doGet)
{
   ptime start = microsec_clock::local_time ();
   while (true)
   {
      const int val = Atomically (bind (ExtractInt, ref (v), doSet, doGet, _1));
      if ((microsec_clock::local_time () - start) > boost::posix_time::seconds (5))
      {
         boost::lock_guard<boost::mutex> lock (s_outputMutex);
         std::cout << threadNum << ": " << val << std::endl;
         start = microsec_clock::local_time ();
      }
   }
}

int main ()
{
   logging::initialize ("stm_timing_log.config", "stm_timing_log.txt", logging::Level::ERR);

   WVar<int> v (0);
   boost::thread t1 (ExtractionThread, 1, ref (v), true, false);
   boost::thread t2 (ExtractionThread, 2, ref (v), false, true);
   t1.join ();
   return 0;   
   
   ValueVecPtr values_p (new ValueVec (NUM_VALUES));
   LOG_MEM_ALLOCATION (ValueVec, values_p.get ());
   for (size_t i = 0; i < NUM_VALUES; ++i)
   {
      (*values_p)[i].reset (new WVar<int>(0));
      LOG_MEM_ALLOCATION (WVar, (*values_p)[i].get ());
   }
   volatile WThreadInfo* info_p = new WThreadInfo (1);
   LOG_MEM_ALLOCATION (WThreadInfo, (const void*)info_p);
   std::vector<WThreadInfoIndex> indexes (NUM_THREADS);
   boost::barrier bar (NUM_THREADS);
   
   boost::thread (TestThread, 0, WRITE, ref (info_p), &indexes[0].m_index, values_p, ref (bar));
   for (size_t i = 1; i < NUM_THREADS; ++i)
   {
      boost::thread (TestThread, i, READ, ref (info_p), &indexes[i].m_index, values_p, ref (bar));
   }

   const ptime startTime = microsec_clock::local_time ();
   ptime lastTime = microsec_clock::local_time ();
   while (true)
   {
      boost::this_thread::sleep (ITERATION_TIME - (microsec_clock::local_time () - lastTime));
      lastTime = microsec_clock::local_time ();
      
      WThreadInfo* newInfo_p = new WThreadInfo (info_p->m_index + 1);
      LOG_MEM_ALLOCATION (WThreadInfo, newInfo_p);
      WThreadInfo* oldInfo_p = (WThreadInfo*)InterlockedExchangePointer (&info_p, newInfo_p);

      bool allUpdated = false;
      while (!allUpdated)
      {
         allUpdated = true;
         BOOST_FOREACH (const WThreadInfoIndex& i, indexes)
         {
            if (i.m_index == oldInfo_p->m_index)
            {
               allUpdated = false;
               break;
            }
         }
      }

      double avg = 0.0;
      double min = std::numeric_limits<double>::max ();
      double max = 0.0;
      BOOST_FOREACH (const WThreadResult& res, oldInfo_p->m_results)
      {
         const double rate = res.m_numTransactions/res.m_elapsed;
         avg += rate;
         min = std::min (min, rate);
         max = std::max (max, rate);
      }
      avg /= NUM_THREADS;

      const time_duration time = lastTime - startTime;
      std::cout << time.total_seconds () << ", " << avg << ", " << min << ", " << max << std::endl;

      LOG_MEM_DEALLOCATION (WThreadInfo, oldInfo_p);
      delete oldInfo_p;

      // if ((lastTime - startTime) > seconds (20))
      // {
      //    std::cout << "Done" << std::endl;
      //    boost::this_thread::sleep (seconds (30));
      //    break;
      // }
   }
   
   return 0;
}
