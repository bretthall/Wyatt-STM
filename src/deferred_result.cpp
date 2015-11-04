/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

#include "deferred_result.h"

namespace WSTM
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

         m_done_v.Set (true, at);
         auto connections = m_connections.Get (at);
         if (!connections.empty ())
         {
            at.After ([connections]()
                      {
                         for (auto& conn: connections)
                         {
                            conn.m_callback ();
                         }
                      });
            m_connections.Set (WPersistentList<const WConn> (), at);
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
            Atomically ([&](WAtomic& at){RetryIfNotDone (at, timeout);});
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
         auto connections = m_connections.Get (at);
         const int index = m_connectionIndex.Get (at);
         connections.push_front (WConn (index, callback));
         m_connectionIndex.Set (index + 1);
         m_connections.Set (connections, at);
         return index;
      }
      
      void WDeferredValueCoreBase::Disconnect (const int index, WAtomic& at)
      {
         auto connections = m_connections.Get (at);
         auto it = std::find_if (connections.begin (), connections.end (), [index](const auto& conn) {return (conn.m_index == index);});
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

      WDeferredValueWatch::WDeferredValueWatch (const std::shared_ptr<WDeferredValueCoreBase>& core_p):
         m_core_p (core_p)
      {}
      
      WDeferredValueWatch::~WDeferredValueWatch ()
      {
         if (m_core_p)
         {
            Atomically ([&](WAtomic& at)
                        {
                           if (!m_core_p->IsDone (at))
                           {
                              m_core_p->Fail (WBrokenPromiseError (), at);
                           }
                        });
         }
      }

   }

   WDeferredResult<void> DoneDeferred ()
   {
      WDeferredValue<void> value;
      value.Done ();
      return value;
   }

   WNotDoneError::WNotDoneError ():
      std::runtime_error ("Deferred result is not done yet")
   {}
   
   WAlreadyDoneError::WAlreadyDoneError ():
      std::runtime_error ("Deferred result is already done")
   {}
   
   WInvalidDeferredResultError::WInvalidDeferredResultError ():
      std::runtime_error ("Deferred result is no connected to a deferred value")
   {}
   
   WBrokenPromiseError::WBrokenPromiseError ():
      std::runtime_error ("Deferred value was not set done before destruction")
   {}

}
