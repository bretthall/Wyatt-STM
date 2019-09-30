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

#pragma once

#include "stm.h"
#include "exception_capture.h"
#include "exports.h"
#include "persistent_list.h"

#include <boost/optional.hpp>

/**
 * @file deferred_result.h
 * A deferred result system built on top of the STM system.
 */

namespace WSTM
{
   /**
    * @defgroup DeferredResult Deferred Results
    *
    * A deferred result system built on top of the STM system.
    *
    * This system provides transactional versions of std::future and std::promise. The analogs are
    * WDeferredResult and WDeferredValue respectively. 
    */
   ///@{

   /**
    * Exception thrown by some methods of WDeferredResult if the result is not available yet.
    */
   class WSTM_CLASSAPI WNotDoneError : public WException
   {
   public:
      //!Creates an exception object.
      WNotDoneError ();
   };

   /**
    * Exception thrown by some methods of WDeferredValue if the value is already in the done state.
    */
   class WSTM_CLASSAPI WAlreadyDoneError : public WException
   {
   public:
      //!Creates an exception object.
      WAlreadyDoneError ();
   };
   
   namespace Internal
   {
      class WSTM_CLASSAPI WDeferredValueCoreBase
      {
      public:
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

         bool Wait (const WTimeArg& timeout = WTimeArg::Unlimited ()) const;

         void RetryIfNotDone (WAtomic& at, const WTimeArg& timeout = WTimeArg::Unlimited ()) const;

         void ThrowError (WAtomic& at) const;

         using DoneCallback = std::function<void ()>;
         int Connect (DoneCallback callback, WAtomic& at);
         void Disconnect (const int index, WAtomic& at);

         void AddReader (WAtomic& at);
         void RemoveReader (WAtomic& at);
         bool HasReaders (WAtomic& at) const;
         
      private:
         WVar<bool> m_done_v;
         WExceptionCapture m_failure;
         struct WConn
         {
            int m_index;
            DoneCallback m_callback;
            WConn (const int index, DoneCallback callback);
         };
         WVar<WPersistentList<const WConn> > m_connections;
         WVar<int> m_connectionIndex;
         WVar<int> m_readerCount_v;
      };

      
      template <typename Result_t>
      class WDeferredValueCore : public WDeferredValueCoreBase
      {
      public:
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
      {};

      class WSTM_CLASSAPI WDeferredValueWatch
      {
      public:
         WDeferredValueWatch (const std::shared_ptr<WDeferredValueCoreBase>& core_p);
         ~WDeferredValueWatch ();

      private:
         std::shared_ptr<WDeferredValueCoreBase> m_core_p;
      };

   }
   
   /**
    * Base class for WDeferredValue.
    */
   template <typename Result_t>
   class WDeferredValueBase
   {
   public:
      /**
       * Constructor.
       */
      WDeferredValueBase () :
         m_core_p (std::make_shared<Core>())
      {
         m_watch_p = std::make_shared<Internal::WDeferredValueWatch> (m_core_p);
      }

      /**
       * Copy Constructor.
       */
      WDeferredValueBase (const WDeferredValueBase& value):
         m_core_p (value.m_core_p),
         m_watch_p (value.m_watch_p)
      {}

      /**
       * Copy Assignment Operator
       */
      WDeferredValueBase& operator= (const WDeferredValueBase& value)
      {
         if (this != &value) {
            m_core_p = value.m_core_p;
            m_watch_p = value.m_watch_p;
         }

         return *this;
      }
         
      //@{
      /**
       * Sets the value to the "failed" state with the given reason for failure.
       *
       * @param failure The reason for the failure, this will be thrown by any associated
       * WDeferredResult objects.
       *
       * @param at The current STM transaction.
       */
      template <typename Fail_t>
      void Fail (const Fail_t& failure)
      {
         Atomically ([&](WAtomic& at){Fail (failure, at);});
      }

      template <typename Fail_t>
      void Fail (const Fail_t& failure, WAtomic& at)
      {
         m_core_p->Fail (failure, at);
      }
      //@}

      //@{
      /**
       * Checks if the value has already been set to done.
       *
       * @param The current STM transaction.
       *
       * @return true if already set to done, false otherwise.
       */
      bool IsDone () const
      {
         return Atomically ([&](WAtomic& at){return m_core_p->IsDone (at);});
      }
         
      bool IsDone (WAtomic& at) const
      {
         return m_core_p->IsDone (at);
      }
      //@}

      //@{
      /**
       * Checks if this value has any WDeferredResult objects attached to it.
       *
       * @param The current STM transaction.
       *
       * @return true if there are attached WDeferredResult objects, false otherwise.
       */
      bool HasReaders () const
      {
         return Atomically ([&](WAtomic& at){return HasReaders (at);});
      }
         
      bool HasReaders (WAtomic& at) const
      {
         return m_core_p->HasReaders (at);
      }
      //@}
         
   protected:
      using Core = Internal::WDeferredValueCore<Result_t>;
      using CorePtr = std::shared_ptr<Core>;
      CorePtr m_core_p;

      std::shared_ptr<Internal::WDeferredValueWatch> m_watch_p;
   };

   /**
    * The "write" end of a deferred result pair. This class is used by the operation that is
    * reporting its result.
    *
    * Note that copying WDeferredValue and creating a WDeferredResult object frorm a WDeferredValue
    * object are not threadsafe. If you need to do this from multiple threads then the
    * WDeferredValue must either be protected by mutexs or stored in an WVar (the latter is better).
    */
   template <typename Result_t>
   class WDeferredValue : public WDeferredValueBase<Result_t>
   {
   public:
      template <typename> friend class WDeferredResult;

      /**
       * Creates a WDeferredValue object in the "not done" state.
       */
      WDeferredValue ()
      {}
      
      //@{
      /**
       * Sets the value to the "done" state with the given result.
       *
       * @param res The result that will be reported by associated WDeferredResult objects.
       *
       * @param at The current STM transaction.
       */
      void Done (const Result_t& res)
      {
         Atomically ([&](WAtomic& at){Done (res, at);});
      }
      
      void Done (const Result_t& res, WAtomic& at)
      {
         //gcc requires "this"
         this->m_core_p->Done (res, at);
      }
      //@}
   };

   /**
    * The "write" end of a deferred result pair, see WDeferredResult for more details. This class is
    * used by the operation that is reporting its result. This specialization is for operations that
    * do not reports results, just that they are done or that an error has occured.
    *
    * Note that copying WDeferredValue and creating a WDeferredResult object from a WDeferredValue
    * object are not threadsafe. If you need to do this from multiple threads then the
    * WDeferredValue must either be protected by mutexs or stored in an WVar (the latter is
    * better).
    */
   template <>
   class WDeferredValue<void> : public WDeferredValueBase<void>
   {
   public:
      template <typename> friend class WDeferredResult;

      /**
       * Creates a WDeferredValue object in the "not done" state.
       */
      WDeferredValue ()
      {}
      
      //@{
      /**
       * Sets the value to the "done" state with the given result.
       *
       * @param at The current STM transaction.
       */
      void Done ()
      {
         Atomically ([&](WAtomic& at){Done (at);});
      }
      void Done (WAtomic& at)
      {
         m_core_p->SetDone (at);
      }
      //@}
   };

   
   /**
    * Exception thrown by WDeferredResult methods if the WDeferredResult object is not associated
    * with a WDeferredValue object.
    */
   class WSTM_CLASSAPI WInvalidDeferredResultError : public WException
   {
   public:
      //!Creates an exception object.
      WInvalidDeferredResultError ();
   };

   /**
    * Exception thrown through WDeferredResult if the associated WDeferredValue goes away without
    * setting the result.
    */
   class WSTM_CLASSAPI WBrokenPromiseError : public WException
   {
   public:
      //!Creates an exception object.
      WBrokenPromiseError ();
   };

   /**
    * The read end of a deferred result pair (the other end is WDeferredValue). This is used by
    * operations that need to return immediately but will not have a result available until later,
    * usually because the result is being calculated in another thread. The operation creates a
    * WDeferredValue object that it holds on to and returns a WDeferredResult object that is
    * associated with the WDeferredValue.
    *
    * Note that copying WDeferredResult and creating WDeferredResult objects from a WDeferredValue
    * object are not threadsafe. If you need to do this from multiple threads then the
    * WDeferredValue must either be protected by mutexes or stored in a WVar (the latter is better).
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

      /**
       * Destructor.
       */
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

      //@{
      /**
       * Associates this WDeferredResult object with the same WDeferredValue object as the given
       * WDeferredResult object.
       *
       * @param result The result that is associated with the WDeferredValue object that this object
       * should be associated with.
       * 
       * @param at The current transtaction.
       */
      WDeferredResult (const WDeferredResult& result)
      {
         Atomically ([&](WAtomic& at){Copy (result, at);});
      }
      
      WDeferredResult (const WDeferredResult& result, WAtomic& at)
      {
         Copy (result, at);
      }

      WDeferredResult& operator=(const WDeferredResult& result)
      {
         Atomically ([&](WAtomic& at){Copy (result, at);});
         return *this;
      }

      void Copy (const WDeferredResult& result, WAtomic& at)
      {
         const auto core_p = result.m_core_v.Get (at);
         UpdateReaderCounts (core_p, at);
         m_core_v.Set (core_p, at);
      }
      //@}
      
      //@{
      /**
       * Associates this WDeferredResult object with the given WDeferredValue object. If the
       * WDeferredResult object was already associated with another WDeferredValue object that
       * association is dropped.
       */
      WDeferredResult (const WDeferredValue<Result_t>& value)
      {
         Atomically ([&](WAtomic& at){Init (value, at);});
      }

      WDeferredResult (const WDeferredValue<Result_t>& value, WAtomic& at)
      {
         Init (value, at);
      }

      WDeferredResult& operator=(const WDeferredValue<Result_t>& value)
      {
         Atomically ([&](WAtomic& at){Init (value, at);});
         return *this;
      }

      void Init (const WDeferredValue<Result_t>& value, WAtomic& at)
      {
         UpdateReaderCounts (value.m_core_p, at);
         m_core_v.Set (value.m_core_p, at);
      }
      //@}
      
      //@{
      /**
       * Checks if this object is associated with a WDeferredValue.
       *
       * @param at The current transaction.
       *
       * @return true if this object is associated with a WDeferredValue, false otherwise.
       */
      operator bool() const
      {
         return bool (m_core_v.GetReadOnly ());
      }
      
      bool IsValid (WAtomic& at) const
      {
         return m_core_v.Get (at);
      }
      //@}

      //@{
      /**
       * Releases any existing association with a WDeferredValue object.
       *
       * @param at The current transaction.
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
      //@}
      
      //@{
      /**
       * Checks the "done" state of the associated WDeferredValue object.
       *
       * @return true if the operation is done (either by success or failure).
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool IsDone () const
      {
         return Atomically ([&](WAtomic& at){return IsDone (at);});
      }
      
      bool IsDone (WAtomic& at) const
      {
         return CheckCore (at)->IsDone (at);
      }
      //@}
      
      //@{
      /**
       * Checks the "failed" state of the associated WDeferredValue object.
       *
       * @return true if the operation failed, false otherwise.
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool Failed () const
      {
         return Atomically ([&](WAtomic& at){return Failed (at);});
      }

      bool Failed (WAtomic& at) const
      {
         return CheckCore (at)->Failed (at);
      }
      //@}
      
      /**
       * Waits for the WDeferredValue object to enter the done state (either via success or
       * failure).
       *
       * @param timeout The amount of time to wait.
       *
       * @return true if the result is now available and false if the timeout expired.
       * 
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      bool Wait (const WTimeArg& timeout = WTimeArg::Unlimited (), NO_ATOMIC) const
      {
         const auto core_p = Atomically ([&](WAtomic& at){return CheckCore (at);});
         return core_p->Wait (timeout);
      }

      /**
       * If the result is not available STM::Retry is called, otherwise nothing is done.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to Retry.
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      void RetryIfNotDone (WAtomic& at, const WTimeArg& timeout = WTimeArg::Unlimited ()) const
      {
         CheckCore (at)->RetryIfNotDone (at, timeout);
      }

      //@{
      /**
       * Gets the result of the operation. If operation failed the reason for the failure will be
       * thrown by this method.
       *
       * @param at The current STM transaction.
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       *
       * @throw WNotDoneError if the operation is not done yet.
       */
      Result_t GetResult () const
      {
         return Atomically ([&](WAtomic& at){return GetResult (at);});
      }
      
      Result_t GetResult (WAtomic& at) const
      {
         return CheckCore (at)->GetResult (at);
      }
      //@}

      //@{
      /**
       * If the oepration failed then this method throws the reason for the failure, otherwise it
       * does nothing.
       * 
       * @param at The current STM transaction.
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       *
       * @throw WNotDoneError if the operation is not done yet.
       */
      void ThrowError () const
      {
         Atomically ([&](WAtomic& at){ThrowError (at);});
      }
      
      void ThrowError (WAtomic& at) const
      {
         CheckCore (at)->ThrowError (at);
      }
      //@}

      /**
       * Connection management objects returned by OnDone. This class can be used to disconnect a
       * callback passed to WDeferredResult::OnDone.
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
         
         /**
          * Creates a connection object (Used internally, please ignore).
          */
         WConnection (const int index, const std::shared_ptr<Internal::WDeferredValueCore<Result_t>>& core_p):
            m_index (index), m_core_p (core_p)
         {}

         //@{
         /**
          * Disconnects a callback. It is safe to call this on an uninitialized object.
          *
          * @param The current STM transaction.
          */
         void Disconnect ()
         {
            Atomically ([&](WAtomic& at){Disconnect (at);});
         }
         
         void Disconnect (WAtomic& at)
         {
            auto core_p = m_core_p.lock ();
            if (core_p)
            {
               core_p->Disconnect (m_index, at);
            }
            m_index = -1;
            m_core_p.reset ();
         }
         //@}
         
         /**
          * Checks if the connection is initialized.
          *
          * @return true if initialized, false otherwise.
          */
         operator bool() const
         {
            return m_core_p.lock ();
         }
         
      private:
         int m_index;
         std::weak_ptr<Internal::WDeferredValueCore<Result_t>> m_core_p;
      };

      /**
       * The type that can be passed to OnDone.
       */
      using DoneCallback = Internal::WDeferredValueCoreBase::DoneCallback;

      //@{
      /**
       * Connects a callback that is called when the operation finishes. If the operation has
       * already finished the callback will be called immediately. The callback will only ever be
       * called once.
       *
       * @param callback The callback to register.
       *
       * @param at The current transaction.
       *
       * @return A WConnection object that can be used to disconnect the callback.
       *
       * @throw WInvalidDeferredResultError if the WDeferredResult object is not associated with a
       * WDeferredValue.
       */
      WConnection OnDone (DoneCallback callback)
      {
         return Atomically ([&](WAtomic& at){return OnDone (callback, at);});
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
      //@}
      
   private:
      using Core = Internal::WDeferredValueCore<Result_t>;
      using CorePtr = std::shared_ptr<Core>;
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
   
   /**
    * Creates a WDeferredResult in the "done" state.
    */
   WDeferredResult<void> WSTM_LIBAPI DoneDeferred ();
   
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

   ///@}

}
