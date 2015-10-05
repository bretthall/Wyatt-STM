/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2004-2013. All rights reserved.
 ****************************************************************************/

#include "stdafx.h"

#include "BSS/Thread/STM/WorkQueueSTM.h"

#include "BSS/Common/STLUtils.h"
using bss::STLUtils::IsType;
#include "BSS/Logger/Logging.h"

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4503 4512 4640 6011)
#include <boost/format.hpp>
#include <boost/bind.hpp>
using boost::bind;
using boost::ref;
using boost::str;
using boost::format;
using boost::recursive_mutex;
using boost::condition;
#pragma warning (pop)

#include <algorithm>
#include <deque>
#include <iterator>
#include <vector>

namespace
{
	bss::logging::WLogger& logger = bss::logging::getLogger ("bss::thread::STM::WorkQueue");
}

namespace bss
{
namespace thread
{
namespace STM
{
void WorkQueue::handleMessages(condition& startupCondition,
                               bss::thread::STM::WReadOnlyChannel<MsgPtr> chan)
{
	recursive_mutex::scoped_lock startupLock(m_shutdownMutex);

   bss::thread::STM::WChannelReader<MsgPtr> reader;
   try
   {
      reader.Init (chan);
   }
   catch (bss::thread::STM::WInvalidChannelError&)
   {
      LOG_ERROR (logger, "Got invalid channel");
      startupCondition.notify_one();
      return;
   }

	//create queue and notify start() that we are ready to receive
	//messages
	startupCondition.notify_one();
	startupLock.unlock();
	std::deque<MsgPtr> msgs;
	bool shutdown = false;
	while(true)
	{
		if(!shutdown)
		{
			if(msgs.empty())
			{
				LOG_DEBUG(logger, "Waiting for messages");
				reader.Wait();
				LOG_DEBUG(logger, "Got messages");
			}
			
			//get new messages and check for shutdown message
			LOG_DEBUG(logger, "Checking for new messages");
			recursive_mutex::scoped_lock shutdownLock(m_shutdownMutex);
			std::vector<MsgPtr> newMsgs = reader.ReadAll ();
			LOG_DEBUG(logger, str(format("Got %1% new messages") % newMsgs.size()));
			std::vector<MsgPtr>::iterator it =
				std::find_if(newMsgs.begin(), newMsgs.end(), IsType<Shutdown>());
			if(it != newMsgs.end())
			{
				//get rid of queue and notify stop() that we
				//aren't accepting messages anymore
				LOG_DEBUG(logger, "Got shutdown messsage");
				shutdown = true;
				Shutdown* msg_p = dynamic_cast<Shutdown*>(it->get());
				msg_p->shutdown.notify_one();
				LOG_DEBUG(logger, "Queue shutdown");
				if(msg_p->immediately)
				{
					//ditch out without processing remaining
					//messages while notifying stop() that we have
					//processed the shutdown message.
					LOG_DEBUG(logger, "Thread exiting immedaitely");
					shutdownLock.unlock();
					recursive_mutex::scoped_lock lock(msg_p->processedMutex);
					msg_p->processed.notify_one();
					return;
				}
				++it;  //need to include shutdown message in
				//messages that are copied
			}
			msgs.insert(msgs.end(), newMsgs.begin(), it);
			shutdownLock.unlock();
		}

		LOG_DEBUG(logger, "Processing message");
		MsgPtr msg_p = msgs.front();
		Shutdown* shutdown_p = dynamic_cast<Shutdown*>(msg_p.get());
		if(shutdown_p != 0)
		{
			//We have now processed the messages that were in the
			//queue when stop() was called so we can exit.  Notify
			//stop() that we have processed the shutdown message.
			assert(shutdown);  //should have found Shutdown mesage
			//when it was first sent
			LOG_DEBUG(logger, "Thread exiting");
			recursive_mutex::scoped_lock lock(shutdown_p->processedMutex);
			shutdown_p->processed.notify_one();
			return;
		}
		try
		{
			msg_p->run();
		}
		catch(...)
		{
			LOG_DEBUG(logger,
					  str(format("WorkQueue %1% caught unsigned-handled exception from message %2%") %
						  m_name % typeid(*msg_p).name()));
			//fall through so further messages can be handled
		}
		msgs.pop_front();
	}
}

void WorkQueue::start()
{
		
	//only one of start or stop can be running at a time
	recursive_mutex::scoped_lock startStopLock(m_startStopMutex);
	if(!m_running)
	{
		//start thread and wait to be notified that it is ready to
		//accept messages
		recursive_mutex::scoped_lock startupLock(m_shutdownMutex);
		condition startupCondition;
		m_thread_p.reset(
			new bss::thread::WThread(str(format("WorkQueue: %1%") % m_name),
                                  boost::bind(&WorkQueue::handleMessages,
                                              this,
                                              ref(startupCondition),
                                              bss::thread::STM::WReadOnlyChannel <MsgPtr>(m_channel))));
		startupCondition.wait(startupLock);
		m_running = true;
		LOG_DEBUG(logger, "WorkQueue started");
	}
	else
	{
		LOG_WARN(logger, "Attempt to start already running WorkQueue");
	}
}
	
void WorkQueue::stop(bool wait, bool immediately)
{
	//only one of start or stop can be running at a time
	recursive_mutex::scoped_lock startStopLock(m_startStopMutex);
	if(m_running)
	{
		//send the shutdown message
		recursive_mutex::scoped_lock shutdownLock(m_shutdownMutex);
		Shutdown* msg_p = new Shutdown(immediately);
		MsgPtr sentMsg_p(msg_p);
		recursive_mutex::scoped_lock processedLock(msg_p->processedMutex, boost::defer_lock);
		if(wait)
		{
			processedLock.lock();
		}
		m_channel.Write(sentMsg_p);
		//wait for thread to notify that is has shutdown the
		//message queue (thread could still be running right now
		//processing its remaining messages)
		msg_p->shutdown.wait(shutdownLock);
		if(wait)
		{
			msg_p->processed.wait(processedLock);
		}
		m_thread_p.reset();
		m_running = false;
		LOG_DEBUG(logger, "WorkQueue stopped");
	}
	else
	{
		LOG_WARN(logger, "Attempt to stop WorkQueue that wasn't running");
	}
		
}

void WorkQueue::logBadRun()
{
	LOG_WARN(logger, "Attempt to run operation in WorkQueue that is not running");
}

} // namespace STM
} // namespace thread
} // namespace bss
