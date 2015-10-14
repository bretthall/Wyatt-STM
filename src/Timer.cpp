/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "Timer.h"

namespace bss { namespace thread { namespace STM
{

   WTimer::WTimer ():
      m_start_v (boost::chrono::steady_clock::now ())
   {}
   
   void WTimer::Restart ()
   {
      Atomically ([&](WAtomic& at){Restart (at);});
   }
   
   void WTimer::Restart (WAtomic& at)
   {
      m_start_v.Set (boost::chrono::steady_clock::now (), at);
   }
   
   double WTimer::ElapsedSeconds () const
   {
      return Atomically ([&](WAtomic& at){return ElapsedSeconds (at);});
   }
   
   double WTimer::ElapsedSeconds (WAtomic& at) const
   {
      return boost::chrono::duration<double>(ElapsedDefault (at)).count ();
   }

   boost::chrono::nanoseconds WTimer::ElapsedDefault (WAtomic& at) const
   {
      const auto now = boost::chrono::steady_clock::now ();
      const auto start = m_start_v.Get (at);
      return (now - start);
   }

   boost::chrono::steady_clock::time_point WTimer::GetStart () const
   {
      return Atomically ([&](WAtomic& at){return GetStart (at);});
   }
   
   boost::chrono::steady_clock::time_point WTimer::GetStart (WAtomic& at) const
   {
      return m_start_v.Get (at);
   }

}}}
