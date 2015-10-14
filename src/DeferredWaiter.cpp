/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/DeferredWaiter.h"

#include "BSS/Thread/Thread.h"

#ifdef WIN32
#pragma warning (disable: 4127 4239 4244 4265 4503 4512 4640 6011)
#endif

#include <boost/bind.hpp>

namespace bss { namespace thread { namespace STM
{
   WDeferredWaiter::WDeferredWaiter ():
      m_shutdown_pv (dw_new WVar<bool>(false))
   {
      WChannel <WWait::Ptr> newWaits;
      m_newWaits.Init (newWaits);
      WChannelReader<WWait::Ptr> reader (newWaits);
      bss::thread::WThread (
         "DeferredWaiter",
         boost::bind (WDeferredWaiter::DoDeferredWaiting, reader, m_shutdown_pv));
   }

   WDeferredWaiter::~WDeferredWaiter ()
   {
      m_shutdown_pv->Set (true);
   }

   WDeferredWaiter::Tag::~Tag ()
   {}

   WDeferredWaiter::WWait::WWait () :
      m_done (false),
      m_cancelled_v (false),
      m_cancelled (false)
   {}

   void WDeferredWaiter::WWait::Cancel ()
   {
      m_cancelled_v.Set (true);
   }

   void WDeferredWaiter::WWait::Cancel (WAtomic& at)
   {
      m_cancelled_v.Set (true, at);
   }

   void WDeferredWaiter::DoDeferredWaiting (WChannelReader<WWait::Ptr>& newWaits,
                                            const WVar<bool>::Ptr& shutdown_pv)
   {
      std::list<WWait::Ptr> waits;
      std::vector<WWait::Ptr> newWaitObjs;
      while (Atomically (boost::bind (WDeferredWaiter::DoDeferredWaitingAt,
                                      boost::ref (waits),
                                      boost::ref (newWaitObjs),
                                      boost::ref (newWaits),
                                      boost::cref (shutdown_pv),
                                      _1)))
      {}
   }

   bool WDeferredWaiter::DoDeferredWaitingAt (std::list<WWait::Ptr>& waits,
                                              std::vector<WWait::Ptr>& newWaitObjs,
                                              WChannelReader<WWait::Ptr>& newWaits,
                                              const WVar<bool>::Ptr& shutdown_pv,
                                              bss::thread::STM::WAtomic& at)
   {
      if (shutdown_pv->Get (at))
      {
         return false;
      }

      newWaitObjs.clear ();
      newWaitObjs = newWaits.ReadAll (at);

      bool haveDone = false;
      BOOST_FOREACH (const WWait::Ptr& wait_p, waits)
      {
         if (wait_p->Check (at))
         {
            haveDone = true;
         }         
      }
      BOOST_FOREACH (const WWait::Ptr& wait_p, newWaitObjs)
      {
         if (wait_p->Check (at))
         {
            haveDone = true;
         }         
      }

      if (haveDone)
      {
         at.After (boost::bind (&WDeferredWaiter::CleanupDeferreds,
                                boost::ref (waits),
                                boost::ref (newWaitObjs)));
      }
      else if (!newWaitObjs.empty ())
      {
         at.After (boost::bind (&WDeferredWaiter::AddDeferreds,
                                boost::ref (waits),
                                boost::ref (newWaitObjs)));
      }
      else
      {
         Retry (at);
      }

      return true;
   }

   void WDeferredWaiter::CleanupDeferreds (std::list<WWait::Ptr>& waits,
                                           std::vector<WWait::Ptr>& newWaitObjs)
   {
      for (std::list<WWait::Ptr>::iterator it = waits.begin (); it != waits.end ();)
      {
         if ((*it)->m_done)
         {
            if (!(*it)->m_cancelled)
            {
               try
               {
                  (*it)->Call ();
               }
               catch(...)
               {
                  //do nothing - just here to prevent a misbehaving
                  //handler from ending our thread
               }
            }
            it = waits.erase (it);
         }
         else
         {
            ++it;
         }
      }

      BOOST_FOREACH (const WWait::Ptr& wait_p, newWaitObjs)
      {
         if (wait_p->m_done)
         {
            if (!wait_p->m_cancelled)
            {
               try
               {
                  wait_p->Call ();
               }
               catch(...)
               {
                  //do nothing - just here to prevent a misbehaving
                  //handler from ending our thread
               }
            }
         }
         else
         {
            waits.push_back (wait_p);
         }
      }
      newWaitObjs.clear ();
   }
   
   void WDeferredWaiter::AddDeferreds (std::list<WWait::Ptr>& waits,
                                       std::vector<WWait::Ptr>& newWaitObjs)
   {
      std::copy (newWaitObjs.begin (), newWaitObjs.end (), std::back_inserter (waits));
      newWaitObjs.clear ();
   }

}}}
