/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2010. All rights reserved.
****************************************************************************/

#pragma once

#include "BSS/Thread/STM/DeferredResult.h"
#include "BSS/Thread/STM/ExceptionCaptureAtomic.h"

#include "BSS/Logger/Logging.h"

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <boost/bind.hpp>
#include <boost/utility.hpp>

namespace bss { namespace thread { namespace STM
{
   /**
      Throw this exception from a function that is passed to
      RunInThread() in order to cause the WDeferredResult returned
      by RunInThread() to be set to "failed".  The exception
      passed to this class' constructor is the exception that
      will be thrown by the WDeferredResult returned by RunInThread.
   */
   class WRunInThreadError : public WExceptionCaptureAtomic
   {
   public:
      template <typename Wrapped_t>
      WRunInThreadError(const Wrapped_t& reason)
      {
         Capture(reason);
      }
   };

   /**
    * Exception thrown by the WDeferredResult returned by RunInThread
    * if the operation run by RunInThread throws an exception other
    * than WRunInThreadError.
    */
   class WRunInThreadUnknownError
   {};

   namespace Internal
   {
      //Used internally by runInThread
      template<typename Result_t>
      struct WRunOperation
      {
         static void Run(boost::function<Result_t ()> operation,
                         WDeferredValue<Result_t>& result)
         {
            result.Done(operation());
         }
      };

      template<>
      struct WRunOperation<void>
      {
         static void Run(boost::function<void ()> operation,
                         WDeferredValue<void>& result)
         {
            operation();
            result.Done();
         }
      };
      
      template<typename Result_t>
      void RunInThread(boost::function<Result_t ()> operation,
                       WDeferredValue<Result_t>& result,
                       boost::shared_ptr<boost::barrier> barrier_p)
      {
         bss::logging::WLogger& s_Logger = bss::logging::getLogger ("bss::thread::RunInThread");

         LOG_DEBUG (s_Logger, "Notifying all run-in-thread waiters");
         barrier_p->wait ();
         barrier_p.reset ();
         try
         {
            LOG_DEBUG (s_Logger, "Running the run-in-thread operation");
            WRunOperation<Result_t>::Run (operation, result);
            LOG_DEBUG (s_Logger, "Run in thread operation done");
         }
         catch (WRunInThreadError& exc)
         {
            LOG_DEBUG (s_Logger, "Run-in-thread operation threw exception");
            result.Fail (exc);
         }
         catch (...)
         {
            LOG_DEBUG (s_Logger, "Run-in-thread operation threw unknown exception");
            result.Fail (WRunInThreadUnknownError ());
         }
      }   
   }

   /**
      Runs the given opeartion in another thread.  The result of the
      operation is reported via a WDeferredResult object that is
      returned by this function.  When the passed in operation
      completes the return value is set as the success value of the
      WDeferredResult. If the operation throws a RunInThreadError
      exception then the reason member of the exception object is set
      as the failure reason of the WDeferredResult. Throwing an
      exception other than RunInThreadError will result in
      WRunInThreadUnknownError being set as the failure. Func_t must
      be a callable with signature "result_type ()" and it must be
      compatible with boost::result_of.
   */
   template<typename Func_t>
   WDeferredResult<typename std::result_of<Func_t ()>::type>
   RunInThread(Func_t operation)
   {
      typedef typename std::result_of<Func_t ()>::type ResultType;
      
      bss::logging::WLogger& s_Logger = bss::logging::getLogger ("bss::thread::RunInThread");
      
      //We should probably be doing some sort of thread pooling here
      //instead of always spawning a new thread, but we'll leave
      //that until there's a demonstrated performance need for that
      //much work.

      //We need to wait until the thread is started so that it has
      //copies of the operation and the WDeferredValue before we
      //return.
      LOG_DEBUG (s_Logger, "Running in thread");
      boost::shared_ptr<boost::barrier>barrier_p (new boost::barrier (2));
      WDeferredValue<ResultType> result;
      LOG_DEBUG (s_Logger, "Launching thread");
      boost::thread thr (boost::bind (Internal::RunInThread<ResultType>,
                                    boost::function<ResultType ()>(operation),
                                    result,
                                    barrier_p));
      LOG_DEBUG (s_Logger, "waiting for run-in-thread operation to start");
      barrier_p->wait ();
      LOG_DEBUG (s_Logger, "returning run-in-thread deferred result");
      return result;
   }
   
}}}


