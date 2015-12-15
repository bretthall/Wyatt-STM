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

//Stress test the system for correctness

#include "stm.h"
using namespace WSTM;

#include <boost/format.hpp>
using boost::format;
using boost::str;
#include <boost/program_options.hpp>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <functional>
#include <algorithm>
#include <random>

#ifdef NON_APPLE_CLANG 
//Clang on linux is missing this
extern "C" int __cxa_thread_atexit(void (*func)(), void *obj, void *dso_symbol)
{
   int __cxa_thread_atexit_impl(void (*)(), void *, void *);
   return __cxa_thread_atexit_impl(func, obj, dso_symbol);
}
#endif //NON_APPLE_CLANG 

using PostUpdateFunc = std::function<void ()>;

class WUpdator
{
public:
   virtual void Read (WInconsistent& i);
   virtual PostUpdateFunc Update (WAtomic& at) = 0;
   virtual void CheckValue () const = 0;
};

void WUpdator::Read (WInconsistent&)
{}

class WUpdatorInt : public WUpdator
{
public:
   WUpdatorInt ();
   virtual void Read (WInconsistent& i) override;
   virtual PostUpdateFunc Update (WAtomic& at) override;
   virtual void CheckValue () const override;

private:
   std::atomic<unsigned int> m_lastInconsistentValue;
   WVar<unsigned int> m_value_v;
   std::shared_ptr<std::atomic<unsigned int>> m_expected_p;
};

WUpdatorInt::WUpdatorInt ():
   m_lastInconsistentValue (0),
   m_value_v (0),
   m_expected_p (std::make_shared<std::atomic<unsigned int>>(0))
{}

struct WBadInconsistentValue
{   
   unsigned int m_last;
   unsigned int m_current;
};

void WUpdatorInt::Read (WInconsistent& i)
{
   auto last = m_lastInconsistentValue.load ();
   const auto value = m_value_v.GetInconsistent (i);
   if (value < last)
   {
      //the vars only increase in value so we should never see a value that is less than the last we saw
      throw WBadInconsistentValue {last, value};
   }
   //update the last value unless someone else has already put in a value larger than ours
   while ((last < value) && !m_lastInconsistentValue.compare_exchange_strong (last, value))
   {}
}

PostUpdateFunc WUpdatorInt::Update (WAtomic& at)
{
   m_value_v.Set (m_value_v.Get (at) + 1, at);
   return [expected_p = m_expected_p](){expected_p->fetch_add (1);};   
}

struct WBadIntError
{   
   unsigned int m_expected;
   unsigned int m_actual;
};

void WUpdatorInt::CheckValue () const
{
   const auto expected = m_expected_p->load ();
   const auto value = m_value_v.GetReadOnly ();
   if (value != expected)
   {
      throw WBadIntError {expected, value};
   }
}

class WUpdatorAfter : public WUpdator
{
public:
   WUpdatorAfter ();
   virtual PostUpdateFunc Update (WAtomic& at) override;
   virtual void CheckValue () const override;

private:
   std::atomic<unsigned int> m_value;
   std::shared_ptr<std::atomic<unsigned int>> m_expected_p;
};

WUpdatorAfter::WUpdatorAfter ():
   m_value (0),
   m_expected_p (std::make_shared<std::atomic<unsigned int>>(0))
{}

PostUpdateFunc WUpdatorAfter::Update (WAtomic& at)
{
   at.After ([&](){m_value.fetch_add (1);});
   return [expected_p = m_expected_p](){expected_p->fetch_add (1);};   
}

struct WBadAfterError
{   
   unsigned int m_expected;
   unsigned int m_actual;
};

void WUpdatorAfter::CheckValue () const
{
   const auto expected = m_expected_p->load ();
   const auto value = m_value.load ();
   if (value != expected)
   {
      throw WBadAfterError {expected, value};
   }
}

struct WContext
{
   WContext ();
   
   unsigned int m_minThreads;
   unsigned int m_maxThreads;
   unsigned int m_minVars;
   unsigned int m_maxVars;
   unsigned int m_durationSecs;
   unsigned int m_exitSpawnChance;
   
   std::atomic<unsigned int> m_numThreads;
   
   std::atomic<bool> m_pause;
   std::mutex m_pauseMutex;
   std::condition_variable m_pauseCondition;
   std::condition_variable m_threadPausedCondition;
   unsigned int m_numThreadsPaused;
   void CheckPause ();
   struct WPauseLock
   {
      std::unique_lock<std::mutex> m_lock;
      WContext* m_context_p;
      WPauseLock (WContext& context);
      ~WPauseLock ();
   };

   std::mutex m_varsMutex;
   using VarVec = std::vector<std::shared_ptr<WUpdator>>;
   using VarVecConstPtr = std::shared_ptr<const VarVec>;
   VarVecConstPtr m_vars_p;
   VarVecConstPtr GetVars ();
   template <typename Func_t>
   void UpdateVars (Func_t&& func)
   {
      std::lock_guard<std::mutex> lock (m_varsMutex);
      m_vars_p = func (m_vars_p);
   }
};

WContext::WContext ():
   m_numThreads (0),
   m_pause (false),
   m_numThreadsPaused (0)
{}

void WContext::CheckPause ()
{
   if (m_pause.load ())
   {
      auto lock = std::unique_lock<std::mutex>(m_pauseMutex);
      ++m_numThreadsPaused;
      m_threadPausedCondition.notify_one ();
      m_pauseCondition.wait (lock);
   }
}

WContext::WPauseLock::WPauseLock (WContext& context):
   m_lock (context.m_pauseMutex),
   m_context_p (&context)
{
   m_context_p->m_pause = true;
   while (m_context_p->m_numThreadsPaused < m_context_p->m_numThreads.load ())
   {
      m_context_p->m_threadPausedCondition.wait (m_lock);
   }
}

WContext::WPauseLock::~WPauseLock ()
{
   m_context_p->m_numThreadsPaused = 0;
   m_context_p->m_pauseCondition.notify_all ();
}

WContext::VarVecConstPtr WContext::GetVars ()
{
   std::lock_guard<std::mutex> lock (m_varsMutex);
   return m_vars_p;
}

bool UpdateVars (WContext& context, std::mt19937& mt)
{
   const auto vars_p = context.GetVars ();
   auto dist = std::uniform_int_distribution<size_t>(0, vars_p->size () - 1);
   const auto numChanges = 2*dist (mt);
   auto changes = WContext::VarVec (numChanges);
   std::generate (std::begin (changes), std::end (changes), [&](){return (*vars_p)[dist (mt)];});
   auto postUpdates = Atomically ([&](WAtomic& at)
                                  {
                                     auto postUpdates = std::vector<PostUpdateFunc>();
                                     postUpdates.reserve (numChanges);
                                     std::transform (std::begin (changes), std::end (changes),
                                                     std::back_inserter (postUpdates),
                                                     [&](const auto& change_p){return change_p->Update (at);});
                                     return postUpdates;
                                  });
   for (auto& update: postUpdates)
   {
      update ();
   }

   return false;
}

bool ReadInconsitent (WContext& context, std::mt19937& mt)
{
   const auto vars_p = context.GetVars ();
   auto dist = std::uniform_int_distribution<size_t>(0, vars_p->size () - 1);
   const auto numReads = 2*dist (mt);
   auto reads = WContext::VarVec (numReads);
   std::generate (std::begin (reads), std::end (reads), [&](){return (*vars_p)[dist (mt)];});
   Inconsistently ([&](WInconsistent& i)
                   {
                      for (const auto& read_p: reads)
                      {
                         read_p->Read (i);
                      }
                   });

   return false;
}

bool RetryOnVars (WContext& context, std::mt19937& mt)
{
   const auto vars_p = context.GetVars ();
   auto dist = std::uniform_int_distribution<size_t>(0, vars_p->size () - 1);
   const auto numReads = 2*dist (mt);
   auto vars = WContext::VarVec (numReads);
   std::generate (std::begin (vars), std::end (vars), [&](){return (*vars_p)[dist (mt)];});
   auto tried = false;
   try
   {
      Atomically ([&](WAtomic& at)
                  {
                     if (!tried)
                     {
                        tried = true;
                        for (const auto& var_p: vars)
                        {
                           //NOTE: we aren't capturing the post-update actions here, when we retry
                           //we should be rolling back what was done in the update and thus
                           //shouldn't need to run the post-update actions
                           var_p->Update (at);
                        }
                        Retry (at, std::chrono::milliseconds (200));
                     }
                  });
   }
   catch (WRetryTimeoutException&)
   {}
   
   return false;
}

bool MaybeRemoveVar (WContext& context, std::mt19937& mt)
{
   auto dist = std::uniform_int_distribution<unsigned int>(0, context.m_exitSpawnChance);
   if (dist (mt) == 0)
   {
      context.UpdateVars ([&context, &mt](auto& vars_p) -> WContext::VarVecConstPtr
                          {
                             if (vars_p->size () > context.m_minVars)
                             {
                                auto newVars_p = std::make_shared<WContext::VarVec>(*vars_p);
                                auto dist = std::uniform_int_distribution<size_t>(0, vars_p->size () - 1);
                                newVars_p->erase (std::begin (*newVars_p) + dist (mt));
                                return newVars_p;
                             }
                             else
                             {
                                return vars_p;
                             }
                          });
   }

   return false;
}

bool MaybeAddVar (WContext& context, std::mt19937& mt)
{
   auto dist = std::uniform_int_distribution<unsigned int>(0, context.m_exitSpawnChance);
   if (dist (mt) == 0)
   {
      context.UpdateVars ([&context, &mt](auto& vars_p) -> WContext::VarVecConstPtr
                          {
                             if (vars_p->size () < context.m_maxVars)
                             {
                                auto newVars_p = std::make_shared<WContext::VarVec>(*vars_p);
                                auto dist = std::uniform_int_distribution<unsigned int>(0, 2);
                                if (dist (mt) == 0)
                                {
                                   newVars_p->push_back (std::make_shared<WUpdatorAfter>());
                                }
                                else
                                {
                                   newVars_p->push_back (std::make_shared<WUpdatorInt>());                                   
                                }
                                return newVars_p;
                             }
                             else
                             {
                                return vars_p;
                             }
                          });
   }

   return false;
}

bool MaybeExitThread (WContext& context, std::mt19937& mt)
{
   auto exitDist = std::uniform_int_distribution<unsigned int>(0, context.m_exitSpawnChance);
   return (exitDist (mt) == 0);
}

void RunTest (WContext& context);

void StartThread (WContext& context)
{
   auto oldNumThreads = context.m_numThreads.load ();
   while ((oldNumThreads < context.m_maxThreads) && !context.m_numThreads.compare_exchange_strong (oldNumThreads, oldNumThreads + 1))
   {}
   if (oldNumThreads < context.m_maxThreads)
   {
      std::thread t ([&context](){RunTest (context);});
      t.detach ();
   }      
}

bool MaybeSpawnThread  (WContext& context, std::mt19937& mt)
{
   auto startDist = std::uniform_int_distribution<unsigned int>(0, context.m_exitSpawnChance);
   if (startDist (mt) == 0)
   {
      StartThread (context);
   }

   return false;
}

using Action = std::function<bool (WContext&, std::mt19937&)>;
const std::vector<Action> actions = {UpdateVars, ReadInconsitent, RetryOnVars, MaybeRemoveVar, MaybeAddVar, MaybeExitThread, MaybeSpawnThread};
   
void RunTest (WContext& context)
{
   auto mt = std::mt19937 (std::random_device ()());
   auto actionDist = std::uniform_int_distribution<size_t>(0, actions.size () - 1);
   
   for (;;)
   {
      context.CheckPause ();

      if (actions[actionDist (mt)](context, mt))
      {
         //check to see if we can exit while leaving running threads 
         auto oldNumThreads = context.m_numThreads.load ();
         while ((oldNumThreads > context.m_minThreads) && !context.m_numThreads.compare_exchange_strong (oldNumThreads, oldNumThreads - 1))
         {}
         if (oldNumThreads > context.m_minThreads)
         {
            //we weren't the only thread running so exit

            //need to notify the validation thread in case we're the last thread that it is waiting for
            std::lock_guard<std::mutex> lock (context.m_pauseMutex);
            context.m_threadPausedCondition.notify_one ();
            return;
         }
      }
   }
}


int main (int argc, const char** argv)
{
   const auto numHWThreads = std::thread::hardware_concurrency ();
   WContext context;
   namespace po = boost::program_options;
   po::options_description desc;
   desc.add_options ()
      ("help", "Display help message")
      ("version", "The program and library version")
      ("minThreads,t", po::value<unsigned int>(&context.m_minThreads)->default_value (1), "The minimum number of threads to run")
      ("maxThreads,T", po::value<unsigned int>(&context.m_maxThreads)->default_value (2*numHWThreads), "The maximum number of threads to run")
      ("minVars,v", po::value<unsigned int>(&context.m_minVars)->default_value (5), "The minimum number of vars to use")
      ("maxVars,V", po::value<unsigned int>(&context.m_maxVars)->default_value (20), "The maximum number of vars to use")
      ("duration,D", po::value<unsigned int>(&context.m_durationSecs)->default_value (5), "The number of seconds between checkpoints")
      ("chance,C", po::value<unsigned int>(&context.m_exitSpawnChance)->default_value (20),
       "The chance that a thread will exit or a new thread will start on any given thread iteration (1 in C chance)");
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

   std::cout << "Starting run:" << std::endl;
   std::cout << "\tminThreads = " << context.m_minThreads << std::endl;
   std::cout << "\tmaxThreads = " << context.m_maxThreads << std::endl;
   std::cout << "\tminVars = " << context.m_minVars << std::endl;
   std::cout << "\tmaxVars = " << context.m_maxVars << std::endl;
   std::cout << "\tduration = " << context.m_durationSecs << std::endl;

   context.UpdateVars ([&](const auto&)
                       {
                          auto newVars_p = std::make_shared<WContext::VarVec>(std::max (context.m_maxVars/2, context.m_minVars));
                          std::generate (std::begin (*newVars_p), std::end (*newVars_p), [](){return std::make_shared<WUpdatorInt>();});

                          auto mt = std::mt19937 (std::random_device ()());
                          auto dist = std::uniform_int_distribution<size_t>(0, newVars_p->size () - 1);
                          (*newVars_p)[dist (mt)] = std::make_shared<WUpdatorAfter>();
                          (*newVars_p)[dist (mt)] = std::make_shared<WUpdatorAfter>();
                          (*newVars_p)[dist (mt)] = std::make_shared<WUpdatorAfter>();                          
                          return newVars_p;
                       });
                       
   const auto numThreads = std::max (context.m_maxThreads/2 + 1, context.m_minThreads);
   for (unsigned int i = 0; i < numThreads; ++i)
   {
      StartThread (context);
   }

   const auto checkpointDelay = std::chrono::seconds (context.m_durationSecs);
   for (;;)
   {
      std::this_thread::sleep_for (checkpointDelay);
      
      WContext::WPauseLock lock (context);
      const auto vars_p = context.GetVars ();
      for (const auto& var_p: *vars_p)
      {
         var_p->CheckValue ();
      }
      std::cout << str (format ("%1% threads, %2% vars") % context.m_numThreads.load () % vars_p->size ()) << std::endl;
   }

   
   return 0;
}
