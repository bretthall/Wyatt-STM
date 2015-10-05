/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "LifetimeMonitor.h"

namespace bss { namespace thread { namespace STM
{

   WLifetimeBeacon::WLifetimeBeacon ():
      m_alive_v (std::make_shared<WVar<bool>>(true))
   {}
   
   WLifetimeBeacon::~WLifetimeBeacon ()
   {
      m_alive_v.GetReadOnly ()->Set (false);
   }

   WLifetimeBeacon::WLifetimeBeacon (WLifetimeBeacon&& b)
   {
      Atomically ([&](WAtomic& at)
                  {
                     m_alive_v.Set (b.m_alive_v.Get (at), at);                     
                     b.m_alive_v.Set (std::make_shared<WVar<bool>>(true), at);
                  });
   }
   
   WLifetimeBeacon& WLifetimeBeacon::operator=(WLifetimeBeacon&& b)
   {
      Atomically ([&](WAtomic& at)
                  {
                     auto alive_p = m_alive_v.Get (at);
                     if (alive_p)
                     {
                        alive_p->Set (false, at);
                     }
                     m_alive_v.Set (b.m_alive_v.Get (at));
                     b.m_alive_v.Set (std::make_shared<WVar<bool>>(true), at);
                  });
      return *this;
   }

   void WLifetimeBeacon::Reset ()
   {
      Atomically ([&](WAtomic& at){Reset (at);});
   }

   void WLifetimeBeacon::Reset (WAtomic& at)
   {
      auto alive_p = m_alive_v.Get (at);
      if (alive_p)
      {
         alive_p->Set (false, at);
      }
      m_alive_v.Set (std::make_shared<WVar<bool>>(true), at);      
   }

   WLifetimeMonitor::WLifetimeMonitor ()
   {}
   
   WLifetimeMonitor::WLifetimeMonitor (const WLifetimeBeacon& beacon)
   {
      Monitor (beacon);
   }
   
   void WLifetimeMonitor::Monitor (const WLifetimeBeacon& beacon)
   {
      Atomically ([&](WAtomic& at){Monitor (beacon, at);});
   }

   WLifetimeMonitor::WLifetimeMonitor (const WLifetimeMonitor& m):
      m_alive_v (m.m_alive_v.GetReadOnly ())
   {}
   
   WLifetimeMonitor& WLifetimeMonitor::operator=(const WLifetimeMonitor& m)
   {
      Atomically ([&](WAtomic& at){m_alive_v.Set (m.m_alive_v.Get (at), at);});
      return *this;
   }

   WLifetimeMonitor::WLifetimeMonitor (WLifetimeMonitor&& m):
      m_alive_v (std::move (m.m_alive_v))
   {}
   
   WLifetimeMonitor& WLifetimeMonitor::operator=(WLifetimeMonitor&& m)
   {
      m_alive_v = std::move (m.m_alive_v);
      return *this;
   }

   void WLifetimeMonitor::Monitor (const WLifetimeBeacon& beacon, WAtomic& at)
   {
      m_alive_v.Set (beacon.m_alive_v.Get (at), at);
   }

   bool WLifetimeMonitor::IsAlive () const
   {
      return Atomically ([&](WAtomic& at){return IsAlive (at);});
   }
   
   bool WLifetimeMonitor::IsAlive (WAtomic& at) const
   {
      const auto alive_p = m_alive_v.Get (at);
      return (alive_p && alive_p->Get (at));
   }

}}}
