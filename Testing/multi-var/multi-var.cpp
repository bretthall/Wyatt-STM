/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

//Tests how the number of STM vars influences performance.
//STM::WVar uses bss::thread_specific_ptr heavily and this could be
//leading to performance degradation when there are a lot of WVar's in
//play.

#include <BSS/Thread/STM/stm.h>
using namespace  bss::thread::STM;

#include <boost/timer.hpp>
#include <boost/bind.hpp>
using boost::bind;
using boost::ref;
#include <boost/foreach.hpp>

#include <iostream>

const size_t VAR_INC = 50;
const size_t NUM_TRANS = 40;


void WriteTrans (std::vector<WVar<int>*>& vars, WAtomic& at)
{
   BOOST_FOREACH (WVar<int>* var_p, vars)
   {
      const int i = var_p->Get (at);
      var_p->Set (i + 1, at);
   }
}

int main ()
{
   std::vector<WVar<int>*> vars;
   size_t subInc = 1;
   
   while (true)
   {
      vars.reserve (vars.size () + VAR_INC);
      for (size_t i = 0; i < VAR_INC; ++i)
      {
         vars.push_back (new WVar<int>(0));
      }
      //These writes will make sure that all the vars have tss pointers
      Atomically (bind (WriteTrans, ref (vars), _1));

      //We only time the update of VAR_INC vars so that we have the
      //same workload each time. If tss isn't messing things up then
      //the time to do the updates should not increase as we add more
      //vars to the system. The vars that are updated are spread
      //throughout the existing vars (if we always used the first set
      //then we woudl not see an impact from the linear link list used
      //in tss).
      std::vector<WVar<int>*> varsSubset;
      for (size_t i = 0; i < vars.size (); i += subInc)
      {
         varsSubset.push_back (vars[i]);
      }
      assert (varsSubset.size () == 50);
      ++subInc;
      
      boost::timer t;
      for (size_t i = 0; i < NUM_TRANS; ++i)
      {
         Atomically (bind (WriteTrans, ref (varsSubset), _1));         
      }      
      const double elapsed = t.elapsed ();
      std::cout << vars.size () << ","
                << elapsed << std::endl;

   }
   
   return 0;
}
