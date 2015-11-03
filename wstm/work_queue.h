/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "BSS/Thread/STM/Channel.h"
#include "BSS/Thread/STM/DeferredResult.h"
#include "BSS/Thread/Thread.h"

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4503 4512 4640 6011)
#include <boost/shared_ptr.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/utility.hpp>
#pragma warning (pop)

namespace bss
{
namespace thread
{
namespace STM
{

/**
 * Exception thrown through a WDeferredResult if a workqueue operation
 * throws something other than its declared type of exception.
 */
class BSS_CLASSAPI WWorkQueueUnknownError
{};

class BSS_CLASSAPI WorkQueue
{
public:
	WorkQueue(const std::string& name = ""):
		m_name(name),
		m_running(false)
	{}

	//Starts the worker thread.
	void start();

	bool isRunning() const {return m_running;}
	
	//Stops the worker thread.
	//	wait - If true this method will block until the worker
	//		   thread exits.
	//	immediately - If true the worker thread will exit
	//				  immediately and not process any remaining
	//				  operations that were in the queue before stop()
	//				  was called.
	void stop(bool wait = true, bool immediately = false);

	struct RunError
	{
		RunError(const std::string& msg_):
			msg(msg_)
		{}

		const std::string msg;
	};
	
	//Adds the given operation to the queue to be executed in the
	//worker thread.
	//	Error_t - The error type of the operation, ie the operation
	//			  throws exceptions of the type Error_t and only
	//			  exceptions of this type.  If this is void then the
	//			  operation cannot throw exceptions.  Must be
	//			  explicitly specified.
	//	Func_t - The type of function object to run.  Must have a
	//			 result_type typedef and take no arguments. 		  
	//	operation - The operation to execute.  Can only throw
	//				exceptions of the type Error_t or cannot throw
	//				exceptions if Error_t is void.
	template<typename Error_t, typename Func_t>
	bss::thread::STM::WDeferredResult<typename boost::result_of<Func_t ()>::type> run(Func_t operation)
	{
		typedef typename boost::result_of<Func_t ()>::type ResultType;
		
		boost::recursive_mutex::scoped_lock lock(m_shutdownMutex);
		if(!m_running)
		{
			logBadRun();
			throw RunError("Attempted use of WorkQueue that was not running");
		}

		Run<ResultType, Error_t>* run_p =
         new Run<ResultType, Error_t>(boost::function<ResultType()>(operation));
		MsgPtr msg_p = MsgPtr(run_p);
      m_channel.Write (msg_p);
		return run_p->m_result;
	}
	
private:
	const std::string m_name;
	bool m_running;
	boost::shared_ptr<bss::thread::WThread> m_thread_p;

	struct BSS_CLASSAPI MsgBase
	{
		virtual void run() = 0;
	};
	typedef boost::shared_ptr<MsgBase> MsgPtr;
   bss::thread::STM::WChannel<MsgPtr> m_channel;

	template <typename Result_t, typename Error_t>
	struct Run : public MsgBase
	{
		typedef bss::thread::STM::WDeferredValue<Result_t> Val;
		typedef bss::thread::STM::WDeferredResult<Result_t> Res;

		Run(boost::function<Result_t ()>& operation):
			m_operation(operation)
		{
         m_result.Init (m_value);
      }

		virtual void run()
		{
			RunOp<Result_t, Error_t>::run(m_operation, m_value);
		}

		template <typename Res_t, typename Err_t>
		struct RunOp
		{
			static void run(boost::function<Res_t ()>& operation, Val& result)
			{
				try
				{
					result.Done(operation());
				}
				catch(Err_t& err)
				{
					result.Fail(err);
				}
				catch(...)
				{
					result.Fail(WWorkQueueUnknownError ());
					throw;
				}
			}
		};
		
		template <typename Err_t>
		struct RunOp<void, Err_t>
		{
			static void run(boost::function<void ()>& operation, Val& result)
			{
				try
				{
					operation();
					result.Done();
				}
				catch(Err_t& err)
				{
					result.Fail(err);
				}
				catch(...)
				{
					result.Fail(WWorkQueueUnknownError ());
					throw;
				}
			}
		};
		
		template <typename Res_t>
		struct RunOp<Res_t, void>
		{
			static void run(boost::function<Res_t ()>& operation, Val& result)
			{
				try
				{
					result.Done(operation());
				}
				catch(...)
				{
					result.Fail (WWorkQueueUnknownError);
					throw;
				}
			}
		};
		
		template <>
		struct RunOp<void, void>
		{
			static void run(boost::function<void ()>& operation, Val& result)
			{
				try
				{
					operation();
					result.Done();
				}
				catch(...)
				{
					result.Fail(WWorkQueueUnknownError ());
					throw;
				}
			}
		};
		
		boost::function<Result_t ()> m_operation;
		Res m_result;
      Val m_value;
	};
	
	//message that is sent when the handler thread should shutdown.
	struct Shutdown : public MsgBase
	{
		Shutdown(bool immediately_):
			immediately(immediately_)
		{}

      virtual ~Shutdown () {}

		virtual void run() {}
		
		const bool immediately;
		boost::condition shutdown;
		boost::recursive_mutex processedMutex;
		boost::condition processed;
	};

	void handleMessages(boost::condition& startupCondition,
                       bss::thread::STM::WReadOnlyChannel<MsgPtr> chan);
	void logBadRun();
	
	boost::recursive_mutex m_shutdownMutex;
	boost::recursive_mutex m_startStopMutex;
};

} // namespace STM
} // namespace thread
} // namespace bss