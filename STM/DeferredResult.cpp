/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

#include "stdafx.h"
#include "DeferredResult.h"

#ifdef WIN32
#pragma warning (disable: 4127 4239 4244 4265 4503 4512 4640 6011)
#endif

#include <boost/foreach.hpp>

namespace bss { namespace thread { namespace STM
{
   namespace Internal
   {
      WDeferredValueCoreBase::WDeferredValueCoreBase ():
         m_done_v (false),
         m_connectionIndex (0),
         m_readerCount_v (0)
      {}


      void WDeferredValueCoreBase::SetDone (WAtomic& at)
      {
         if (m_done_v.Get (at))
         {
            throw WAlreadyDoneError ();
         }

         struct WCallDoneCallbacks
         {
            bss::thread::WPersistentList<const WConn> m_connections;
            WCallDoneCallbacks (const bss::thread::WPersistentList<const WConn>& connections):
               m_connections (connections)
            {}

            void operator()()
            {
               BOOST_FOREACH (const WConn& conn, m_connections)
               {
                  conn.m_callback ();
               }
            }
         };

         m_done_v.Set (true, at);
         bss::thread::WPersistentList<const WConn> connections = m_connections.Get (at);
         if (!connections.empty ())
         {
            at.After (WCallDoneCallbacks (connections));
            m_connections.Set (bss::thread::WPersistentList<const WConn> (), at);
         }
      }
      
      bool WDeferredValueCoreBase::IsDone (WAtomic& at) const
      {
         return m_done_v.Get (at);
      }
      
      bool WDeferredValueCoreBase::Failed (WAtomic& at) const
      {
         if (!m_done_v.Get (at))
         {
            throw WNotDoneError ();
         }
         return m_failure.HasCaptured (at);
      }
      
      bool WDeferredValueCoreBase::Wait (const WTimeArg& timeout) const
      {
         try
         {
            Atomically (boost::bind (&WDeferredValueCoreBase::RetryIfNotDone, this, _1, cref (timeout)));
            return true;
         }
         catch (WRetryTimeoutException&)
         {
            return false;
         }
      }
      
      void WDeferredValueCoreBase::RetryIfNotDone (WAtomic& at, const WTimeArg& timeout) const
      {
         if (!m_done_v.Get (at))
         {
            Retry (at, timeout);
         }
      }
      
      void WDeferredValueCoreBase::ThrowError (WAtomic& at) const
      {
         if (!m_done_v.Get (at))
         {
            throw WNotDoneError ();
         }

         m_failure.ThrowCaptured (at);
      }

      int WDeferredValueCoreBase::Connect (DoneCallback callback, WAtomic& at)
      {
         bss::thread::WPersistentList<const WConn> connections = m_connections.Get (at);
         const int index = m_connectionIndex.Get (at);
         connections.push_front (WConn (index, callback));
         m_connectionIndex.Set (index + 1);
         m_connections.Set (connections, at);
         return index;
      }
      
      void WDeferredValueCoreBase::Disconnect (const int index, WAtomic& at)
      {
         bss::thread::WPersistentList<const WConn> connections = m_connections.Get (at);
         struct WIndexIs
         {
            const int m_index;
            WIndexIs (const int index) : m_index (index) {}
            bool operator()(const WConn& conn) const
            {
               return conn.m_index == m_index;
            }
         };
         bss::thread::WPersistentList<const WConn>::iterator it = std::find_if (connections.begin (),
                                                                   connections.end (),
                                                                   WIndexIs (index));
         if (it != connections.end ())
         {
            connections.erase (it);
            m_connections.Set (connections, at);
         }
      }

      WDeferredValueCoreBase::WConn::WConn (const int index, DoneCallback callback):
         m_index (index), m_callback (callback)
      {}

      void WDeferredValueCoreBase::AddReader (WAtomic& at)
      {
         m_readerCount_v.Set (m_readerCount_v.Get (at) + 1, at);
      }
      
      void WDeferredValueCoreBase::RemoveReader (WAtomic& at)
      {
         const auto count = m_readerCount_v.Get (at);
#ifdef _DEBUG
         if (count < 1)
         {
            //A moribund transaction coudl show a count < 1 here, so we have to check again when the
            //transaction commits (it should never commit though)
            at.After ([=](){assert (count > 0);});
         }
#endif
         m_readerCount_v.Set (count - 1, at);
      }
         
      bool WDeferredValueCoreBase::HasReaders (WAtomic& at) const
      {
         return (m_readerCount_v.Get (at) > 0);
      }

      WDeferredValueWatch::WDeferredValueWatch (const WDeferredValueCoreBase::Ptr& core_p):
         m_core_p (core_p)
      {}
      
      WDeferredValueWatch::~WDeferredValueWatch ()
      {
         if (m_core_p)
         {
            const bool done =  Atomically (
               boost::bind (&WDeferredValueCoreBase::IsDone, m_core_p.get (), _1));
            if (!done)
            {
               Atomically (boost::bind (&WDeferredValueCoreBase::Fail<WBrokenPromiseError>,
                                        m_core_p.get (), WBrokenPromiseError (), _1));
            }
         }
      }

   }

   WDeferredResult<void> DoneDeferred ()
   {
      WDeferredValue<void> value;
      value.Done ();
      return value;
   }

}}}
