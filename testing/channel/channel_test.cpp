/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

//Stress test for WChannel and associated classes, a given number of
//readers and writers are run on one channel.

const int NUM_READERS = 2;
const int NUM_WRITERS = 2;
const int EXIT_CHANCE_NUM = 1;
const int EXIT_CHANCE_DEN = 1000;
const double DURATION = 100000.0;

#include <BSS/Thread/STM/Channel.h>
#include <BSS/Thread/Thread.h>

using namespace  bss::thread::STM;

#include <boost/format.hpp>
using boost::format;
using boost::str;
#include <boost/bind.hpp>
using boost::bind;
using boost::ref;
#include <boost/timer.hpp>

#include <iostream>

bool ShouldExit ()
{
   return ((rand () % EXIT_CHANCE_DEN) < EXIT_CHANCE_NUM);
}

void DecrementCount (WVar<int>& count_v, WAtomic& at)
{
   const int oldCount = count_v.Get (at);
   assert (oldCount > 0);
   if (oldCount > 0)
   {
      count_v.Set (oldCount - 1, at);
   }
}

void IncrementCount (WVar<int>& count_v, WAtomic& at)
{
   const int oldCount = count_v.Get (at);
   count_v.Set (oldCount + 1, at);
}

struct WReader
{
   WChannelReader<int> m_reader;
   WVar<int>& m_numReaders_v;
   WVar<bool>& m_done_v;
   
   WReader (WChannelReader<int> reader, WVar<int>& numReaders_v, WVar<bool>& done_v);

   void operator()();
   bool DoRead (WAtomic& at);
};

WReader::WReader (WChannelReader<int> reader, WVar<int>& numReaders_v, WVar<bool>& done_v):
   m_reader (reader), m_numReaders_v (numReaders_v), m_done_v (done_v)
{
   Atomically (bind (IncrementCount, ref (m_numReaders_v), _1));
}

void WReader::operator()()
{
   while (Atomically (bind (&WReader::DoRead, this, _1)))
   {}
}

bool WReader::DoRead (WAtomic& at)
{
   if (m_done_v.Get (at) || ShouldExit ())
   {
      m_reader.ReadAll (at);
      m_reader.Release (at);
      DecrementCount (m_numReaders_v, at);
      return false;
   }

   m_reader.Read ();
   return true;
}

struct WWriter
{
   int m_nextVal;
   WChannelWriter<int> m_writer;
   WVar<int>& m_numWriters_v;
   WVar<bool>& m_done_v;
   
   WWriter (WChannelWriter<int> reader, WVar<int>& numReaders_v, WVar<bool>& done_v);

   void operator()();
   bool DoWrite (WAtomic& at);
};

WWriter::WWriter (WChannelWriter<int> reader, WVar<int>& numReaders_v, WVar<bool>& done_v):
   m_nextVal (0),
   m_writer (reader),
   m_numWriters_v (numReaders_v),
   m_done_v (done_v)
{
   Atomically (bind (IncrementCount, ref (m_numWriters_v), _1));
}

void WWriter::operator()()
{
   while (Atomically (bind (&WWriter::DoWrite, this, _1)))
   {}
}

bool WWriter::DoWrite (WAtomic& at)
{
   if (m_done_v.Get (at) || ShouldExit ())
   {
      DecrementCount (m_numWriters_v, at);
      return false;
   }

   m_writer.Write (m_nextVal);
   ++m_nextVal;
   return true;
}

void StartReaders (const int num, WVar<int>& num_v, WChannel<int>& chan, WVar<bool>& done_v)
{
   std::cout << "Starting " << num << " readers" << std::endl;
   
   for (int i = 0; i < num; ++i)
   {
      bss::thread::WThread ("reader", WReader (chan, num_v, done_v));
   }
}

void StartWriters (const int num, WVar<int>& num_v, WChannel<int>& chan, WVar<bool>& done_v)
{
   std::cout << "Starting " << num << " writers" << std::endl;

   for (int i = 0; i < num; ++i)
   {
      bss::thread::WThread ("writer", WWriter (chan, num_v, done_v));
   }
}

void UpdateReaderWriters (WVar<int>& numReaders_v,
                          WVar<int>& numWriters_v,
                          WChannel<int>& chan,
                          WVar<bool>& done_v,
                          WAtomic& at)
{
   bool retry = true;
   
   const int numReaders = numReaders_v.Get (at);
   if (numReaders < NUM_READERS)
   {
      retry = false;
      at.After (bind (StartReaders,
                      NUM_READERS - numReaders,
                      ref (numReaders_v),
                      ref (chan),
                      ref (done_v)));
   }

   const int numWriters = numWriters_v.Get (at);
   if (numWriters < NUM_WRITERS)
   {
      retry = false;
      at.After (bind (StartWriters,
                      NUM_WRITERS - numWriters,
                      ref (numWriters_v),
                      ref (chan),
                      ref (done_v)));
   }

   if (retry)
   {
      Retry (at, boost::posix_time::seconds (1));
   }
}

void WaitForThreadExits (WVar<int>& numReaders_v, WVar<int>& numWriters_v, WAtomic& at)
{
   if ((numReaders_v.Get (at) > 0) || (numWriters_v.Get (at) > 0))
   {
      Retry (at);
   }
}

void main ()
{
   boost::scoped_ptr<WChannel<int> > chan_p (new WChannel<int>);
   WVar<int> numReaders_v (0);
   WVar<int> numWriters_v (0);
   WVar<bool> done_v (false);
   
   boost::timer t;
   while (t.elapsed () < DURATION)
   {
      try
      {
         Atomically (
            bind (UpdateReaderWriters,
                  ref (numReaders_v),
                  ref (numWriters_v),
                  ref (*chan_p),
                  ref (done_v),
                  _1));
      }
      catch(WRetryTimeoutException&)
      {
         //ignore, just retyring so that timer can be checked
      }
   }

   done_v.Set (true);
   std::cout << "Waiting for thread exits" << std::endl;
   Atomically (bind (WaitForThreadExits, ref (numReaders_v), ref (numWriters_v), _1));

   chan_p.reset ();
   std::cout << "Remaining nodes = " << Internal::GetNumNodes () << std::endl;

   const size_t maxNodeNum = Internal::GetMaxNodeNum ();
   const std::vector<size_t> nums = Internal::GetExistingNodeNums ();
}
