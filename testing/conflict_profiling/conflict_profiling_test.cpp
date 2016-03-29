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

//Generates test data for the conflict profiler

#include "stm.h"

#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>

#include <random>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <fstream>
#include <array>

constexpr size_t maxThreads = 9;
const char* threadName1 = "thread1";
const char* threadName2 = "thread2";
const char* threadName3 = "thread3";
const char* threadName4 = "thread4";
const char* threadName5 = "thread5";
const char* threadName6 = "thread6";
const char* threadName7 = "thread7";
const char* threadName8 = "thread8";
const char* threadName9 = "thread9";
std::array<const char*, maxThreads> threadNames = {threadName1, threadName2, threadName3, threadName4, threadName5, threadName6, threadName7, threadName8, threadName9};

constexpr size_t maxTxns = 9;
const char* txnName1 = "txn1";
const char* txnName2 = "txn2";
const char* txnName3 = "txn3";
const char* txnName4 = "txn4";
const char* txnName5 = "txn5";
const char* txnName6 = "txn6";
const char* txnName7 = "txn7";
const char* txnName8 = "txn8";
const char* txnName9 = "txn9";
std::array<const char*, maxTxns> txnNames = {txnName1, txnName2, txnName3, txnName4, txnName5, txnName6, txnName7, txnName8, txnName9};

constexpr auto maxNumVars = 9;

const char* varName1 = "var1";
const char* varName2 = "var2";
const char* varName3 = "var3";
const char* varName4 = "var4";
const char* varName5 = "var5";
const char* varName6 = "var6";
const char* varName7 = "var7";
const char* varName8 = "var8";
const char* varName9 = "var9";
std::array<const char*, maxNumVars> varNames = {varName1, varName2, varName3, varName4, varName5, varName6, varName7, varName8, varName9};

auto var1 = WSTM::WVar<int>(0);
auto var2 = WSTM::WVar<int>(0);
auto var3 = WSTM::WVar<int>(0);
auto var4 = WSTM::WVar<int>(0);
auto var5 = WSTM::WVar<int>(0);
auto var6 = WSTM::WVar<int>(0);
auto var7 = WSTM::WVar<int>(0);
auto var8 = WSTM::WVar<int>(0);
auto var9 = WSTM::WVar<int>(0);
std::array<WSTM::WVar<int>*, maxNumVars> vars = {&var1, &var2, &var3, &var4, &var5, &var6, &var7, &var8, &var9};

void NameVars ()
{
   for (auto i = size_t (0); i < vars.size (); ++i)
   {
      vars[i]->NameForConflictProfiling (varNames[i]);
   }
}

struct WAttempt
{
   int m_txnIndex;
   std::chrono::high_resolution_clock::time_point m_start;
   std::chrono::high_resolution_clock::time_point m_finish;
   std::array<int, maxNumVars> m_set;
   std::array<int, maxNumVars> m_got;
};

enum class CommitOrConflict
{
   commit,
   conflict
};

struct WAttemptInfo
{
   CommitOrConflict m_result;
   WAttempt m_attempt;
};

auto attemptResults = std::deque<WAttemptInfo>();
std::mutex attemptResultsMutex;
std::condition_variable attemptResultsCondition;

void WriteVars (const std::array<int, maxNumVars>& vs, const unsigned int numVars, std::ofstream& out)
{
   out << varNames[vs[0]];
   for (auto i = 1u; i < numVars; ++i)
   {
      out << '/' << varNames[vs[i]];
   }
}

auto OutputResults (const unsigned int numVarsRead, const unsigned int numVarsWritten)
{
   return [=]()
   {
      std::ofstream out ("test_output.csv");
      out << "Result,TxnId,Set,Got,Start,Finish" << std::endl;

      std::unique_lock<std::mutex> lock (attemptResultsMutex);
      for (;;)
      {
         if (!attemptResults.empty ())
         {
            const auto attempt = attemptResults.front ();
            attemptResults.pop_front ();
            lock.unlock ();
            if (attempt.m_attempt.m_txnIndex > -1)
            {
               out << ((attempt.m_result == CommitOrConflict::commit) ? "commit" : "conflict") << ','
                   << txnNames[attempt.m_attempt.m_txnIndex] << ',';
               WriteVars (attempt.m_attempt.m_set, numVarsWritten, out);
               out << ',';
               WriteVars (attempt.m_attempt.m_got, numVarsRead, out);
               out << ','
                   << std::chrono::duration_cast<std::chrono::microseconds>(attempt.m_attempt.m_start.time_since_epoch ()).count () << ','
                   << std::chrono::duration_cast<std::chrono::microseconds>(attempt.m_attempt.m_finish.time_since_epoch ()).count () << std::endl;
            }
            else
            {
               break;
            }
            lock.lock ();
         }
         else
         {
            attemptResultsCondition.wait (lock);
         }
      }
   };
}

void OutputConflict (const WAttempt& attempt)
{
   std::lock_guard<std::mutex> lock (attemptResultsMutex);
   attemptResults.push_back ({CommitOrConflict::conflict, attempt});
   attemptResultsCondition.notify_one ();
}

void OutputCommit (const WAttempt& attempt)
{
   std::lock_guard<std::mutex> lock (attemptResultsMutex);
   attemptResults.push_back ({CommitOrConflict::commit, attempt});
   attemptResultsCondition.notify_one ();
}

struct WRandom
{
   std::mt19937 m_gen;
   std::uniform_int_distribution<> m_dist;

   WRandom (const unsigned int numVars):
      m_dist (0, numVars - 1)
   {}

   int GetNum ()
   {
      return m_dist (m_gen);
   }
};

int SetAVar (WRandom& rnd, const int index, WSTM::WAtomic& at)
{
   auto varIndex = rnd.GetNum ();
   vars[varIndex]->Set (index, at);
   return varIndex;
}

int GetAVar (WRandom& rnd, WSTM::WAtomic& at)
{
   auto varIndex = rnd.GetNum ();
   vars[varIndex]->Get (at);
   return varIndex;
}

const auto attemptLimit = 10;

#define DEF_TRANSCTION(index) void Txn##index (const unsigned int numVars, const unsigned int numVarsToRead, const unsigned int numVarsToWrite) \
   {                                                                    \
      static WRandom rnd (numVars);                                     \
      auto lastAttempt_o = boost::optional<WAttempt> ();                \
      auto numAttempts = 0;                                             \
      WSTM::Atomically ([&](WSTM::WAtomic& at)                          \
                        {                                               \
                           WSTM::ConflictProfiling::NameTransaction (txnName##index); \
                           if (lastAttempt_o)                           \
                           {                                            \
                              OutputConflict (*lastAttempt_o);          \
                           }                                            \
                           else                                         \
                           {                                            \
                              lastAttempt_o = WAttempt ();              \
                              lastAttempt_o->m_txnIndex = index - 1;    \
                           }                                            \
                           lastAttempt_o->m_start = std::chrono::high_resolution_clock::now (); \
                           for (auto i = 0u; i < numVarsToWrite; ++i)   \
                           {                                            \
                              lastAttempt_o->m_set[i] = SetAVar (rnd, index, at); \
                           }                                            \
                           if (numAttempts < attemptLimit)              \
                           {                                            \
                              for (auto i = 0u; i < numVarsToRead; ++i) \
                              {                                         \
                                 lastAttempt_o->m_got[i] = GetAVar (rnd, at); \
                              }                                         \
                           }                                            \
                           ++numAttempts;                               \
                           lastAttempt_o->m_finish = std::chrono::high_resolution_clock::now (); \
                        });                                             \
      OutputCommit (*lastAttempt_o);                                    \
   }//

DEF_TRANSCTION (1);
DEF_TRANSCTION (2);
DEF_TRANSCTION (3);
DEF_TRANSCTION (4);
DEF_TRANSCTION (5);
DEF_TRANSCTION (6);
DEF_TRANSCTION (7);
DEF_TRANSCTION (8);
DEF_TRANSCTION (9);
std::array<std::function<void (unsigned int, unsigned int, unsigned int)>, maxTxns> txns = {Txn1, Txn2, Txn3, Txn4, Txn5, Txn6, Txn7, Txn8, Txn9};

std::thread CreateThread (const int index, const unsigned int numIters, const unsigned int numVars, const unsigned int numVarsToRead, const unsigned int numVarsToWrite)
{
   return std::thread (
      [=]()
      {
         WSTM::ConflictProfiling::NameThread (threadNames[index]);
         for (auto i = 0u; i < numIters; ++i)
         {
            txns[index](numVars, numVarsToRead, numVarsToWrite);
         }
      });
}

int main (int argc, const char** argv)
{
   auto numThreads = 0u;
   auto numVars = 0u;
   auto numVarsToRead = 0u;
   auto numVarsToWrite = 0u;
   auto numIters = 0u;
   
   namespace po = boost::program_options;
   po::options_description desc;
   desc.add_options ()
      ("help", "Display help message")
      ("threads,t", po::value<unsigned int>(&numThreads)->default_value (4), "The number of threads to run")
      ("iters,i", po::value<unsigned int>(&numIters)->default_value (1000), "The number of iterations to run per thread")
      ("vars,v", po::value<unsigned int>(&numVars)->default_value (4), "The number of vars to use (must be at least the number of threads)")
      ("readVars,r", po::value<unsigned int>(&numVarsToRead)->default_value (2), "The number of vars to read on each iteration")
      ("writeVars,w", po::value<unsigned int>(&numVarsToWrite)->default_value (2), "The number of vars to write on each iteration");
   po::variables_map vm;
   po::store(po::parse_command_line(argc, argv, desc), vm);
   po::notify(vm);
   if (vm.count("help")) {
      std::cout << desc << std::endl;
      return 0;
   }
   if (numThreads > maxThreads)
   {
      numThreads = maxThreads;
      std::cout << "The maximum number of threads is 9, using " << numThreads << " threads" << std::endl;      
   }
   if (numVars < numThreads)
   {
      numVars = numThreads;
      std::cout << "vars must be at least equal to the number of threads, using " << numVars << " vars" << std::endl;
   }
   if (numVarsToRead > numVars)
   {
      numVarsToRead = numVars;
      std::cout << "readVars can't be greater than vars, using " << numVarsToRead << " readVars" << std::endl;
   }
   if (numVarsToWrite > numVars)
   {
      numVarsToWrite = numVars;
      std::cout << "writeVars can't be greater than vars, using " << numVarsToWrite << " writeVars" << std::endl;
   }

   NameVars ();

   auto outputThread = std::thread (OutputResults (numVarsToRead, numVarsToWrite));
   auto threads = std::vector<std::thread>();
   for (auto i = 0u; i < numThreads; ++i)
   {
      threads.push_back (CreateThread (i, numIters, numVars, numVarsToRead, numVarsToWrite));
   }

   for (auto& t: threads)
   {
      t.join ();
   }

   {
      std::lock_guard<std::mutex> lock (attemptResultsMutex);
      auto stop = WAttempt ();
      stop.m_txnIndex = -1;
      attemptResults.push_back ({CommitOrConflict::conflict, stop});
      attemptResultsCondition.notify_one ();
   }
   outputThread.join ();
   
   return 0;
}
