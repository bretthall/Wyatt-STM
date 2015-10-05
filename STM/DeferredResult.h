/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "BSS/Thread/STM/STM.h"
#include "BSS/Thread/PersistentList.h"
#include "BSS/Thread/STM/ExceptionCaptureAtomic.h"
#include "BSS/Common/Pointers.h"
#include "BSS/wtcbss.h"

#ifdef WIN32
#pragma warning (push)
#pragma warning (disable: 4127 4239 4244 4265 4503 4512 4640 6011)
#endif

#include <boost/bind.hpp>
#include <boost/function.hpp>

namespace bss { namespace thread { namespace STM
{
   /**
    * Exception thrown by some methods of WDeferredResult if the
    * result is not available yet.
    */
   class BSS_CLASSAPI WNotDoneError
   {};

   /**
    * Exception thrown by some methods of WDeferredValue if the value
    * is already in the done state.
    */
   class BSS_CLASSAPI WAlreadyDoneError
   {};
   
   namespace Internal
   {
      class BSS_CLASSAPI WDeferredValueCoreBase
      {
      public:
         DEFINE_POINTERS (WDeferredValueCoreBase);

         WDeferredValueCoreBase ();

         template <typename Fail_t>
         void Fail (const Fail_t& failure, WAtomic& at)
         {
            SetDone (at);
            m_failure.Capture (failure, at);
         }

         void SetDone (WAtomic& at);
         
         bool IsDone (WAtomic& at) const;

         bool Failed (WAtomic& at) const;

         bool Wait (const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;

         void RetryIfNotDone (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;

         void ThrowError (WAtomic& at) const;

         typedef boost::function<void ()> DoneCallback;
         int Connect (DoneCallback callback, WAtomic& at);
         void Disconnect (const int index, WAtomic& at);

         void AddReader (WAtomic& at);
         void RemoveReader (WAtomic& at);
         bool HasReaders (WAtomic& at) const;
         
      private:
         WVar<bool> m_done_v;
         WExceptionCaptureAtomic m_failure;
         struct WConn
         {
            int m_index;
            DoneCallback m_callback;
            WConn (const int index, DoneCallback callback);
         };
         WVar<bss::thread::WPersistentList<const WConn> > m_connections;
         WVar<int> m_connectionIndex;
         WVar<int> m_readerCount_v;
      };

      
      template <typename Result_t>
      class WDeferredValueCore : public WDeferredValueCoreBase
      {
      public:
         DEFINE_POINTERS (WDeferredValueCore);

         void Done (const Result_t& res, WAtomic& at)
         {
            SetDone (at);
            m_result_v.Set (boost::optional<Result_t>(res), at);
         }

         Result_t GetResult (WAtomic& at) const
         {
            ThrowError (at);
            boost::optional<Result_t> res_o = m_result_v.Get (at);
            if (!res_o)
            {
               throw WNotDoneError ();
            }
            return *res_o;
         }

      private:
         WVar<boost::optional<Result_t> > m_result_v;
      };

      template <>
      class WDeferredValueCore<void> : public WDeferredValueCoreBase
      {
      public:
         DEFINE_POINTERS (WDeferredValueCore);         
      };

      class BSS_CLASSAPI WDeferredValueWatch
      {
      public:
         DEFINE_POINTERS (WDeferredValueWatch);

         WDeferredValueWatch (const WDeferredValueCoreBase::Ptr& core_p);
         ~WDeferredValueWatch ();

      private:
         WDeferredValueCoreBase::Ptr m_core_p;
      };

      template <typename Result_t>
      class WDeferredValueBase
      {
      public:
         WDeferredValueBase () :
            m_core_p (new Core)
         {
            m_watch_p.reset (new WDeferredValueWatch (m_core_p));
         }

         WDeferredValueBase (const WDeferredValueBase& value):
            m_core_p (value.m_core_p),
            m_watch_p (value.m_watch_p)
         {}
         
         //!{
         /**
          * Sets the value to the "failed" state with the given reason
          * for failure.
          *
          * @param failure The reason for the failure, this will be
          * thrown by any associated WDeferredResult objects.
          *
          * @param at The current STM transaction.
          */
         template <typename Fail_t>
         void Fail (const Fail_t& failure)
         {
            Atomically (
               boost::bind (
                  &WDeferredValueBase::FailImpl<Fail_t>, this, boost::cref (failure), _1));
         }

         template <typename Fail_t>
         void Fail (const Fail_t& failure, WAtomic& at)
         {
            FailImpl (failure, at);
         }
         //!}

         //!{
         /**
          * Returns true if the value has already been set to done.
          *
          * @param The current STM transaction.
          *
          * @return true if already set to done, false otherwise.
          */
         bool IsDone () const
         {
            return Atomically (
               boost::bind (&Internal::WDeferredValueCore<Result_t>::IsDone, m_core_p.get (), _1));
         }
         
         bool IsDone (WAtomic& at) const
         {
            return m_core_p->IsDone (at);
         }
         //!}

         //!{
         /**
          * Returns true if this value has any WDeferredResult objects attached to it.
          */
         bool HasReaders () const
         {
            return Atomically ([&](WAtomic& at){return HasReaders (at);});
         }
         
         bool HasReaders (WAtomic& at) const
         {
            return m_core_p->HasReaders (at);
         }
         //!}
         
      protected:
         typedef Internal::WDeferredValueCore<Result_t> Core;
         typedef typename Core::Ptr CorePtr;
         CorePtr m_core_p;

         WDeferredValueWatch::Ptr m_watch_p;
         
      private:
         template <typename Fail_t>
         void FailImpl (const Fail_t& failure, WAtomic& at)
         {
            m_core_p->Fail (failure, at);
         }

      };

   }

   /**
    * The "write" end of a deferred result pair. This class is used by
    * the operation that is reporting its result.
    *
    * Note that copying WDeferredValue and creating WDeferredResult
    * object form a WDeferredValue object are not threadsafe. If you
    * need to do this from multiple threads then the WDeferredValue
    * must either be protected by mutexs or stored in an STM::WVar
    * (the latter is better).
    */
   template <typename Result_t>
   class WDeferredValue : public Internal::WDeferredValueBase<Result_t>
   {
   public:
      template <typename> friend class WDeferredResult;

      /**
       * Creates a WDeferredValue object in the "not done" state.
       */
      WDeferredValue ()
      {}
      
      /**
       * Copies the given WDeferredValue object. When a deferred
       * valuew is copied the objects are associated such that setting
       * one done sets the other done.
       *
       * @param value The value to copy.
       */
      WDeferredValue& operator= (const WDeferredValue& value)
      {
         m_core_p = value.m_core_p;
         return *this;
      }

      //!{
      /**
       * Sets the value to the "done" state with the given result.
       *
       * @param res The result that will be reported by associated
       * WDeferredResult objects.
       *
       * @param at The current STM transaction.
       */
      void Done (const Result_t& res)
      {
         Atomically (boost::bind (&WDeferredValue::DoneImpl, this, boost::cref (res), _1));
      }
      
      void Done (const Result_t& res, WAtomic& at)
      {
         DoneImpl (res, at);
      }
      //!}

   private:
      void DoneImpl (const Result_t& res, WAtomic& at)
      {
         m_core_p->Done (res, at);
      }
      
   };

   /**
    * The "write" end of a deferred result pair, see WDeferredResult
    * for more details. This class is used by the operation that is
    * reporting its result. This specialization is for operations that
    * do not reports results, just that they are done or that an error
    * has occured.
    *
    * Note that copying WDeferredValue and creating WDeferredResult
    * object form a WDeferredValue object are not threadsafe. If you
    * need to do this from multiple threads then the WDeferredValue
    * must either be protected by mutexs or stored in an STM::WVar
    * (the latter is better).
    */
   template <>
   class WDeferredValue<void> : public Internal::WDeferredValueBase<void>
   {
   public:
      template <typename> friend class WDeferredResult;

      /**
       * Creates a WDeferredValue object in the "not done" state.
       */
      WDeferredValue ()
      {}
      
      /**
       * Copies the given WDeferredValue object. When a deferred
       * valuew is copied the objects are associated such that setting
       * one done sets the other done. Note that resetting a
       * WDeferredValue object resets only that object, not any
       * objects that are associated via copying.
       *
       * @param value The value to copy.
       */
      WDeferredValue& operator= (const WDeferredValue& value)
      {
         m_core_p = value.m_core_p;
         return *this;
      }
      
      //!{
      /**
       * Sets the value to the "done" state with the given result.
       *
       * @param res The result that will be reported by associated
       * WDeferredResult objects.
       *
       * @param at The current STM transaction.
       */
      void Done ()
      {
         Atomically (boost::bind (&WDeferredValue::DoneImpl, this, _1));
      }
      void Done (WAtomic& at)
      {
         DoneImpl (at);
      }
      //!}
      
   private:
      void DoneImpl (WAtomic& at)
      {
         m_core_p->SetDone (at);
      }
   };

   /**
    * Exception thrown by WDeferredResult methods if the
    * WDeferredResult object is not associated with a WDeferredValue
    * object.
    */
   class BSS_CLASSAPI WInvalidDeferredResultError
   {};

   /**
    * Exception thrown through WDeferredResult if the associated
    * WDeferredValue goes away without setting the result.
    */
   class BSS_CLASSAPI WBrokenPromiseError
   {};

   /**
    * The read end of a deferred result pair (the other end is
    * WDeferredValue). This is used by operations that need to
    * return immediately but will not have a result available until
    * later, usually because the result is being calculated in another
    * thread. The operation creates a WDeferredValue object that it
    * hodls on to and returns a WDeferredResult object that is
    * associated with the WDeferredValue.
    *
    * Note that copying WDeferredResult and creating WDeferredResult
    * objects from a WDeferredValue object are not threadsafe. If you
    * need to do this from multiple threads then the WDeferredValue
    * must either be protected by mutexes or stored in an STM::WVar
    * (the latter is better).
    */
   template <typename Result_t>
   class WDeferredResult
   {
   public:
      /**
       * Creates an uninitialized WDeferredResult object. This object cannot be used until it has
       * been initialized from a WDeferredValue or another WDeferredResult.
       */
      WDeferredResult ()
      {}

      ~WDeferredResult ()
      {
         Atomically ([&](WAtomic& at)
                     {
                        const auto core_p = m_core_v.Get (at);
                        if (core_p)
                        {
                           core_p->RemoveReader (at);
                        }
                     });
      }

      //!{
      /**
       * Associates this WDeferredResult object with the same
       * WDeferredValue object as the given WDeferredResult object.
       *
       * @param result The result that is associated with the
       * WDeferredValue object that this object should be associated
       * with.
       */
      WDeferredResult (const WDeferredResult& result)
      {
         //for some reason VS2010 barfs if we don't use "this" below
         Atomically ([&](WAtomic& at){this->Copy (result, at);});
      }
      
      WDeferredResult (const WDeferredResult& result, WAtomic& at)
      {
         Copy (result, at);
      }

      WDeferredResult& operator=(const WDeferredResult& result)
      {
         //for some reason VS2010 barfs if we don't use "this" below
         Atomically ([&](WAtomic& at){this->Copy (result, at);});
         return *this;
      }

      void Copy (const WDeferredResult& result, WAtomic& at)
      {
         const auto core_p = result.m_core_v.Get (at);
         UpdateReaderCounts (core_p, at);
         m_core_v.Set (core_p, at);
      }
      //!}
      
      //!{
      /**
       * Associates this WDeferredResult object with the given WDeferredValue object. If the
       * WDeferredResult object was already associated with another WDeferredValue object that
       * association is dropped.
       */
      WDeferredResult (const WDeferredValue<Result_t>& value)
      {
         //for some reason VS2010 barfs if we don't use "this" below
         Atomically ([&](WAtomic& at){this->Init (value, at);});
      }

      WDeferredResult (const WDeferredValue<Result_t>& value, WAtomic& at)
      {
         Init (value, at);
      }

      WDeferredResult& operator=(const WDeferredValue<Result_t>& value)
      {
         //for some reason VS2010 barfs if we don't use "this" below
         Atomically ([&](WAtomic& at){this->Init (value, at);});
         return *this;
      }

      void Init (const WDeferredValue<Result_t>& value, WAtomic& at)
      {
         UpdateReaderCounts (value.m_core_p, at);
         m_core_v.Set (value.m_core_p, at);
      }
      //!}
      
      //!{
      /**
       * Returns true if this object is associated with a
       * WDeferredValue, false otherwise.
       */
      operator bool() const
      {
         return m_core_v.GetReadOnly ();
      }
      
      bool IsValid (WAtomic& at) const
      {
         return m_core_v.Get (at);
      }
      //!}

      //!{
      /**
       * Releases any existing association with a WDeferredValue
       * object.
       */
      void Release ()
      {
         Atomically ([&](WAtomic& at){Release (at);});
      }
      
      void Release (WAtomic& at)
      {
         UpdateReaderCounts (nullptr, at);
         m_core_v.Set (nullptr, at);
      }
      //!}
      
      //!{
      /**
       * Checks the "done" state of the associated WDeferredValue
       * object.
       *
       * @return true if the operation is done (either by success or
       * failure).
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool IsDone () const
      {
         return Atomically (boost::bind (&WDeferredResult::IsDone, this, _1));
      }
      
      bool IsDone (WAtomic& at) const
      {
         return CheckCore (at)->IsDone (at);
      }
      //!}
      
      //!{
      /**
       * Checks the "failed" state of the associated WDeferredValue
       * object.
       *
       * @return true if the operation failed, false otherwise.
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool Failed () const
      {
         return Atomically (boost::bind (&WDeferredResult::Failed, this, _1));
      }

      bool Failed (WAtomic& at) const
      {
         return CheckCore (at)->Failed (at);
      }
      //!}
      
      /**
       * Waits for the WDeferredValue object to enter the done state
       * (either via success or failure).
       *
       * @param timeout The number of milliseconds to wait.
       *
       * @return true if the result is now available and false if the
       * timeout expired.
       * 
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool Wait (const WTimeArg& timeout = WTimeArg::UNLIMITED (), NO_ATOMIC) const
      {
         const auto core_p = Atomically ([&](WAtomic& at){return CheckCore (at);});
         return core_p->Wait (timeout);
      }

      /**
       * If the result is not available STM::Retry is called,
       * otherwise nothing is done.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to Retry.
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      void RetryIfNotDone (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const
      {
         CheckCore (at)->RetryIfNotDone (at, timeout);
      }

      //!{
      /**
       * Gets the result of the operation. If operation failed the
       * reason for the failure will be thrown by this method.
       *
       * @param at The current STM transaction.
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       *
       * @throw WNotDoneError if the operation is not done yet.
       */
      Result_t GetResult () const
      {
         return Atomically (boost::bind (&WDeferredResult::GetResult, this, _1));         
      }
      
      Result_t GetResult (WAtomic& at) const
      {
         return CheckCore (at)->GetResult (at);
      }
      //!}

      //!{
      /**
       * If the oepration failed then this method throws the reason
       * for the failure, otherwise it does nothing.
       * 
       * @param at The current STM transaction.
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       *
       * @throw WNotDoneError if the operation is not done yet.
       */
      void ThrowError () const
      {
         Atomically (boost::bind (&WDeferredResult::ThrowError, this, _1));
      }
      
      void ThrowError (WAtomic& at) const
      {
         CheckCore (at)->ThrowError (at);
      }
      //!}

      /**
       * Connection management objects returned by OnDone. This class
       * can be used to disconnect a callback passed to
       * WDeferredResult::OnDone.
       */
      class WConnection
      {
      public:
         /**
          * Creates an uninitialized WConnection object.
          */
         WConnection ():
            m_index (-1)
         {}
         
         WConnection (const int index,
                      const typename Internal::WDeferredValueCore<Result_t>::Ptr& core_p):
            m_index (index), m_core_p (core_p)
         {}

         //!{
         /**
          * Disconnects a callback. It is safe to call this on an
          * uninitialized object.
          */
         void Disconnect ()
         {
            Atomically (boost::bind (&WConnection::DisconnectImpl, this, _1));
         }
         
         void Disconnect (WAtomic& at)
         {
            DisconnectImpl (at);
         }
         //!}
         
         /**
          * Returns true if the connection is initialized, false
          * otherwise.
          */
         operator bool() const
         {
            return m_core_p.lock ();
         }
         
      private:
         void DisconnectImpl (WAtomic& at)
         {
            Internal::WDeferredValueCore<Result_t>::Ptr core_p = m_core_p.lock ();
            if (core_p)
            {
               core_p->Disconnect (m_index, at);
            }
            m_index = -1;
            m_core_p.reset ();
         }

         int m_index;
         typename Internal::WDeferredValueCore<Result_t>::WPtr m_core_p;
      };

      /**
       * The type that can be passed to OnDone.
       */
      typedef Internal::WDeferredValueCoreBase::DoneCallback DoneCallback;

      //!{
      /**
       * Connects a callback that is called when the operation
       * finishes. If the operation has already finished the callback
       * will be called immediately. The callback will only ever be
       * called once.
       *
       * @param callback The callback to register.
       *
       * @param at The current STM transaction.
       *
       * @return A WConnection object that can be used to disconnect
       * the callback.
       *
       * @throw WInvalidDeferredResultError if the
       * WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      WConnection OnDone (DoneCallback callback)
      {
         return Atomically (boost::bind (&WDeferredResult::OnDone, this, callback, _1));
      }
      
      WConnection OnDone (DoneCallback callback, WAtomic& at)
      {
         const auto core_p = CheckCore (at);
         if (core_p->IsDone (at))
         {
            at.After (callback);
            return WConnection ();
         }
         else
         {
            const int index = core_p->Connect (callback, at);
            return WConnection (index, core_p);             
         }
      }
      //!}
      
   private:
      typedef Internal::WDeferredValueCore<Result_t> Core;
      typedef typename Core::Ptr CorePtr;
      WVar<CorePtr> m_core_v;

      CorePtr CheckCore (WAtomic& at) const
      {
         auto core_p = m_core_v.Get (at);
         if (!core_p)
         {
            throw WInvalidDeferredResultError ();
         }
         return core_p;
      }

      void UpdateReaderCounts (const CorePtr& newCore_p, WAtomic& at)
      {
         const auto core_p = m_core_v.Get (at);
         if (core_p)
         {
            core_p->RemoveReader (at);
         }
         if (newCore_p)
         {
            newCore_p->AddReader (at);
         }
      }      
   };

   //!{
   /**
    * Creates a WDeferredResult in the "done" state.
    *
    * @param res The result to put in the WDeferredResult object.
    */
   template <typename Result_t>
   WDeferredResult<Result_t> DoneDeferred (const Result_t& res)
   {
      WDeferredValue<Result_t> value;
      value.Done (res);
      return value;
   }
   
   WDeferredResult<void> BSS_LIBAPI DoneDeferred ();
   //!}
   
   /**
    * Creates a WDeferredResult object in the "failed" state.
    *
    * @param failure The reason for the failure.
    */
   template <typename Result_t, typename Fail_t>
   WDeferredResult<Result_t> FailDeferred (const Fail_t& failure)
   {
      WDeferredValue<Result_t> value;
      value.Fail (failure);
      return value;      
   }
   
}}}

#ifdef WIN32
#pragma warning (pop)
#endif
