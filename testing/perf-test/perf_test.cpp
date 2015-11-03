/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#include "BSS/Thread/STM/stm.h"
using namespace  bss::thread::STM;

#include <boost/foreach.hpp>
#include <boost/timer/timer.hpp>
#include <boost/range/numeric.hpp>

#include <iostream>
#include <array>
#include <vector>
#include <tuple>
#include <iomanip>

void ReadVars (const std::vector<WVar<int>>& vars, WAtomic& at)
{
   BOOST_FOREACH (const auto& v, vars)
   {
      v.Get (at);
   }
   //read again to test "in transaction" lookup
   BOOST_FOREACH (const auto& v, vars)
   {
      v.Get (at);
   }
}

boost::timer::nanosecond_type RunTest (const size_t numVars, const size_t numReps)
{
   const auto vars = std::vector<WVar<int>>(numVars);
   
   auto timer = boost::timer::cpu_timer ();
   for (auto i = size_t (0); i < numVars; ++i)
   {
      Atomically ([&](WAtomic& at){ReadVars (vars, at);});
   }
   return timer.elapsed ().wall/numVars/numReps;
}

const auto numLoops = size_t (4);

struct WResult
{
   size_t m_numVars;
   size_t m_numReps;
   std::array<boost::timer::nanosecond_type, numLoops> m_timings;

   WResult (const size_t numVars, const size_t numReps) : m_numVars (numVars), m_numReps (numReps) {}
};

std::tuple<boost::timer::nanosecond_type, double> CalcAvgAndSD (const std::array<boost::timer::nanosecond_type, numLoops>& timings)
{
   const auto avg = boost::accumulate (timings, boost::timer::nanosecond_type (0))/numLoops;
   const auto sd = std::sqrt (boost::accumulate (timings, 0.0,
                                                 [=](double cur, boost::timer::nanosecond_type t) -> double
                                                 {
                                                    const auto d = t - avg;
                                                    return (cur + avg*avg);
                                                 })/(numLoops - 1));
   return std::make_tuple (avg, sd);
}

void OutputResult (const WResult& result)
{
   auto avg = boost::timer::nanosecond_type (0);
   auto sd = 0.0;
   std::tie (avg, sd) = CalcAvgAndSD (result.m_timings);
   std::cout << std::setw (4) << result.m_numVars << " Vars: "
             << std::setw (10) << avg << "ns/read (sd = " << sd << ")" << std::endl;
}

int main ()
{
   std::array<WResult, numLoops> results = {WResult (1, 100), WResult (10, 100), WResult (100, 10), WResult (1000, 10)};

   std::cout << "Running tests:" << std::endl;
   
   for (auto i = size_t (0); i < numLoops; ++i)
   {
      std::cout << "Loop " << i;
      BOOST_FOREACH (auto& result, results)
      {
         result.m_timings[i] = RunTest (result.m_numVars, result.m_numReps);
         std::cout << ".";
      }
      std::cout << std::endl;
   }

   std::cout << std::endl;
   BOOST_FOREACH (const auto& result, results)
   {
      OutputResult (result);
   }
   
   return 0;
}
