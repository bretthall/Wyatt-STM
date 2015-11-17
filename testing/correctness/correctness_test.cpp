/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2015. All rights reserved.
****************************************************************************/

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
#include <iostream>
#include <functional>
#include <algorithm>
#include <random>

using PostUpdateFunc = std::function<void ()>;

class WUpdator
{
public:
   virtual PostUpdateFunc Update (WAtomic& at) = 0;
   virtual void CheckValue () const = 0;
};

class WUpdatorInt : public WUpdator
{
public:
   WUpdatorInt ();
   virtual PostUpdateFunc Update (WAtomic& at) override;
   virtual void CheckValue () const override;

private:
   WVar<unsigned int> m_value_v;
   std::shared_ptr<std::atomic<unsigned int>> m_expected_p;
};

WUpdatorInt::WUpdatorInt ():
   m_value_v (0),
   m_expected_p (std::make_shared<std::atomic<unsigned int>>(0))
{}

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

struct WContext
{
   WContext ();
   
   unsigned int m_minThreads;
   unsigned int m_maxThreads;
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
      func (m_vars_p);
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
   auto dist = std::uniform_int_distribution<unsigned int>(0, vars_p->size () - 1);
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
const std::vector<Action> actions = {UpdateVars, MaybeExitThread, MaybeSpawnThread};
   
void RunTest (WContext& context)
{
   auto mt = std::mt19937 (std::random_device ()());
   auto actionDist = std::uniform_int_distribution<unsigned int>(0, actions.size () - 1);
   
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
      ("minThreads,t", po::value<unsigned int>(&context.m_minThreads)->default_value (1), "The minimum number of threads to run")
      ("maxThreads,T", po::value<unsigned int>(&context.m_maxThreads)->default_value (2*numHWThreads), "The maximum number of threads to run")
      ("maxVars,V", po::value<unsigned int>(&context.m_maxVars)->default_value (20), "The maximum number of vars to use")
      ("duration,D", po::value<unsigned int>(&context.m_durationSecs)->default_value (10), "The number of seconds between checkpoints")
      ("chance,C", po::value<unsigned int>(&context.m_exitSpawnChance)->default_value (500),
       "The chance that a thread will exit or a new thread will start on any given thread iteration (1 in C chance)");
   po::variables_map vm;
   po::store(po::parse_command_line(argc, argv, desc), vm);
   po::notify(vm);
   if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 1;
   }

   std::cout << "Starting run:" << std::endl;
   std::cout << "\tminThreads = " << context.m_minThreads << std::endl;
   std::cout << "\tmaxThreads = " << context.m_maxThreads << std::endl;
   std::cout << "\tmaxVars = " << context.m_maxVars << std::endl;
   std::cout << "\tduration = " << context.m_durationSecs << std::endl;
   
   context.UpdateVars ([&](auto& vars_p)
                       {
                          auto newVars_p = std::make_shared<WContext::VarVec>(context.m_maxVars/2);
                          std::generate (std::begin (*newVars_p), std::end (*newVars_p), [](){return std::make_shared<WUpdatorInt>();});
                          vars_p = newVars_p;
                       });
                       
   const auto numThreads = context.m_maxThreads/2 + 1;
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
