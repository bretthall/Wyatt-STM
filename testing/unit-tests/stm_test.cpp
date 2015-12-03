/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "stm.h"

#include <boost/bind.hpp>
#include <boost/format.hpp>
using boost::str;
using boost::format;
#include <boost/test/unit_test.hpp>
#include <boost/thread/barrier.hpp>
using boost::barrier;

#include <cstdlib>
#include <thread>
#include <atomic>


BOOST_AUTO_TEST_SUITE (STM)

namespace
{
	template <typename Type_t>
	struct WVarPtr : public std::shared_ptr<WSTM::WVar<Type_t>>
	{
		WVarPtr()
		{}
						
		WVarPtr(WSTM::WVar<Type_t>* var_p):
			std::shared_ptr<WSTM::WVar<Type_t> >(var_p)
		{}
	};

	typedef std::shared_ptr<boost::barrier> WBarrierPtr;
}

BOOST_AUTO_TEST_SUITE(ExceptionTests)

BOOST_AUTO_TEST_CASE (ExceptionTests_test_cantContinueException)
{
	BOOST_CHECK((boost::is_base_of<
                WSTM::WException,
                WSTM::WCantContinueException>::value));
	const std::string msg = "CantContinueException";
	WSTM::WCantContinueException exc(msg);
	BOOST_CHECK(exc.m_msg == msg);
}

BOOST_AUTO_TEST_CASE (ExceptionTests_test_MaxRetriesException)
{
	BOOST_CHECK((boost::is_base_of<
                WSTM::WCantContinueException,
                WSTM::WMaxRetriesException>::value));
	const unsigned int num = 10;
	const std::string msg = str(format("Hit maximum number of retries (%1%)") % num);
	WSTM::WMaxRetriesException exc(num);
	BOOST_CHECK(exc.m_msg == msg);
}

BOOST_AUTO_TEST_CASE (ExceptionTests_test_MaxConflictsException)
{
	BOOST_CHECK((boost::is_base_of<
                WSTM::WCantContinueException,
                WSTM::WMaxConflictsException>::value));
	const unsigned int num = 10;
	const std::string msg = str(format("Hit maximum number of conflicts (%1%)") % num);
	WSTM::WMaxConflictsException exc(num);
	BOOST_CHECK(exc.m_msg == msg);
}

BOOST_AUTO_TEST_SUITE_END(/*ExceptionTests*/)

BOOST_AUTO_TEST_SUITE(StmVarTests)

namespace 
{
   int varDtorIndex = 0;
   std::vector<std::pair<int, int> > varDtorDead;
   
   struct WVarDtorTester
   {
      WVarDtorTester (const int val) :
         m_val (val),
         m_index (varDtorIndex++)
      {}

      WVarDtorTester (const WVarDtorTester& t) :
         m_val (t.m_val),
         m_index (varDtorIndex++)
      {}
      
      ~WVarDtorTester ();

      int m_val;
      int m_index;
   };
   
   WVarDtorTester::~WVarDtorTester ()
   {
      varDtorDead.push_back (std::make_pair (m_val, m_index));
   }

}

BOOST_AUTO_TEST_CASE (StmVarTests_DtorCalled)
{
   //Makes sure that destructors of objects stored in WVar's have
   //their desatructors called when the WVar is set.
   WSTM::WVar<WVarDtorTester> test (WVarDtorTester (1));
   test.Set (WVarDtorTester (2));
   BOOST_REQUIRE_EQUAL (size_t (3), varDtorDead.size ());
   BOOST_CHECK_EQUAL (1, varDtorDead[0].first);
   BOOST_CHECK_EQUAL (0, varDtorDead[0].second);
   BOOST_CHECK_EQUAL (1, varDtorDead[1].first);
   BOOST_CHECK_EQUAL (1, varDtorDead[1].second);
   BOOST_CHECK_EQUAL (2, varDtorDead[2].first);
   BOOST_CHECK_EQUAL (2, varDtorDead[2].second);
}

namespace
{
	const unsigned int DEFAULT_COMMIT_SLEEP = 30;
	void Wait(std::shared_ptr<boost::barrier>& barrier_p)
	{
		if(0 != barrier_p.get())
		{
			barrier_p->wait();
		}
	}
	
	int IncrementInt(WSTM::WVar<int>& v, const int inc, WSTM::WAtomic& at)
	{
		const int cur = v.Get(at);
		v.Set(cur + inc, at);
		return cur;
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_int_increment)
{
	int cur = 1;
	WSTM::WVar<int> v(cur);
	for(unsigned int i = 0; i < 10; ++i)
	{
		const int inc = std::rand() % 10;
		const int res = WSTM::Atomically(boost::bind(IncrementInt, boost::ref(v), inc, _1));
		BOOST_CHECK_EQUAL(cur, res);
		BOOST_CHECK_EQUAL(cur + inc, v.GetReadOnly());
		cur += inc;
	}
}

namespace
{
	struct WTest
	{
		WTest(int x) : m_i(x) {}
		int m_i;
		typedef std::shared_ptr<const WTest> Ptr;
		typedef WSTM::WVar<Ptr> Var;

		static Ptr create(int x)
		{
			WTest* test_p = new WTest(x);
			return Ptr(test_p);
		}
	};
	
	int IncrementTestClass(WTest::Var& v, const int inc, WSTM::WAtomic& at)
	{
		WTest::Ptr t_p = v.Get(at);
		v.Set(WTest::create(t_p->m_i + inc), at);
		return t_p->m_i;
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_class_increment)
{
	int cur = 1;
	WTest::Var v(WTest::create(cur));
	for(unsigned int i = 0; i < 10; ++i)
	{
		const int inc = std::rand() % 10;
		const int res = WSTM::Atomically(boost::bind(IncrementTestClass, boost::ref(v), inc, _1));
		BOOST_CHECK_EQUAL(cur, res);
		BOOST_CHECK_EQUAL(cur + inc, v.GetReadOnly()->m_i);
		cur += inc;
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_conflict)
{
   WSTM::WVar<int> v1(1);
   WSTM::WVar<int> v2(1);
   boost::barrier barrier(2);
	
	for(unsigned int i = 0; i < 10; ++i)
	{
      const auto old1 = v1.GetReadOnly ();
      const auto old2 = v2.GetReadOnly ();
      
      std::atomic<int> repeat1 (0);
      std::thread t1 ([&]()
         {
            WSTM::Atomically ([&](WSTM::WAtomic& at)
            {
               ++repeat1;
               v1.Set (v1.Get (at) + v2.Get (at), at);
               barrier.wait ();
            });
            barrier.wait ();
         });

		std::atomic<int> repeat2 (0);
      std::thread t2 ([&]()
         {
            WSTM::Atomically ([&](WSTM::WAtomic& at)
            {
               ++repeat2;
               v2.Set (v1.Get (at) + v2.Get (at), at);
               if (repeat2.load () == 1)
               {
                  barrier.wait ();
                  barrier.wait ();
               }
            });
         });
      t1.join ();
      t2.join ();

      BOOST_CHECK_EQUAL (v1.GetReadOnly (), old1 + old2);
      BOOST_CHECK_EQUAL (v2.GetReadOnly (), old1 + 2*old2);
      BOOST_CHECK_EQUAL (repeat1.load (), 1);
      BOOST_CHECK_EQUAL (repeat2.load (), 2);
	}
}

namespace
{
   struct WTestExc
   {};

   void SetVarValueAndThrow (WSTM::WVar<int>& var, const int value, WSTM::WAtomic& at)
   {
      var.Set (value, at);
      throw WTestExc ();
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_exception_thrown)
{
   const int INIT_VAL = 746235;
   WSTM::WVar<int> var (INIT_VAL);
   const int SET_VAL = 932351;
   BOOST_CHECK_THROW ((Atomically (boost::bind (SetVarValueAndThrow,
                                                boost::ref (var),
                                                SET_VAL,
                                                _1))),
                      WTestExc);
   BOOST_CHECK_EQUAL (var.GetReadOnly (), INIT_VAL);
   BOOST_CHECK (!WSTM::InAtomic ());
}

namespace
{
	struct WRetryWaitData
	{
		typedef std::shared_ptr<WRetryWaitData> Ptr;
		
		WRetryWaitData(const std::string& name_, const unsigned int desiredVarVal_):
			name(name_),
			retries(0),
			timeouts(0),
			gotMaxRetries(false),
			var_p(new WSTM::WVar<unsigned int>(0)),
			finishBarrier_p(new boost::barrier(3)),
			retryBarrier_p(new boost::barrier(2)),
			desiredVarVal(desiredVarVal_),
			maxRetries(WSTM::UNLIMITED),
			maxRetryWait(WSTM::UNLIMITED),
			retryTimeout(WSTM::UNLIMITED),
			sleepTime(0),
         m_wasInAtomicAfterTrans (false)
		{}

		void NoRetryBarrier()
		{
			retryBarrier_p.reset();
		}
		
		const std::string name;
		unsigned int retries;
		unsigned int timeouts;
		bool gotMaxRetries;
		std::shared_ptr<WSTM::WVar<unsigned int> > var_p;
		std::shared_ptr<boost::barrier> finishBarrier_p;
		std::shared_ptr<boost::barrier> retryBarrier_p;
		unsigned int desiredVarVal;
		unsigned int maxRetries;
		unsigned int maxRetryWait;
		unsigned int retryTimeout;
		unsigned int sleepTime;
      bool m_wasInAtomicAfterTrans;

   private:
      WRetryWaitData& operator= (const WRetryWaitData&) { return *this; }
	};

	void RetryWaitAtomic(WRetryWaitData::Ptr data_p, WSTM::WAtomic& at)
	{
		const unsigned int i = data_p->var_p->Get(at);
		if(i != data_p->desiredVarVal)
		{
			++data_p->retries;
			Wait(data_p->retryBarrier_p);
			if(0 != data_p->sleepTime)
			{
				boost::this_thread::sleep(boost::posix_time::milliseconds (data_p->sleepTime));
			}
			WSTM::Retry(at, std::chrono::milliseconds (data_p->retryTimeout));
		}
	}

	void RetryWait(WRetryWaitData::Ptr data_p)
	{
		try
		{
			WSTM::Atomically(boost::bind(&RetryWaitAtomic, data_p, _1),
                          WSTM::WMaxRetries(data_p->maxRetries),
                          WSTM::WMaxRetryWait(std::chrono::milliseconds (data_p->maxRetryWait)));
         if (WSTM::InAtomic ())
         {
            data_p->m_wasInAtomicAfterTrans = true;
         }
		}
		catch(WSTM::WRetryTimeoutException&)
		{
			++data_p->timeouts;
         BOOST_CHECK (!WSTM::InAtomic ());
		}
		catch(WSTM::WMaxRetriesException&)
		{
			data_p->gotMaxRetries = true;
         BOOST_CHECK (!WSTM::InAtomic ());
		}
		
		data_p->finishBarrier_p->wait();
	}

	struct WRetryIncData
	{
		typedef std::shared_ptr<WRetryIncData> Ptr;
		
		WRetryIncData(WRetryWaitData::Ptr waitData_p,
                    unsigned int repeats_,
                    unsigned int inc_):
			name(waitData_p->name),
			repeats(repeats_),
			var_p(waitData_p->var_p),
			finishBarrier_p(waitData_p->finishBarrier_p),
			retryBarrier_p(waitData_p->retryBarrier_p),
			inc(inc_),
			sleepTime(0),
         m_wasInAtomicAfterTrans (false)
		{}
		
		const std::string name;
		unsigned int repeats;
		std::shared_ptr<WSTM::WVar<unsigned int> > var_p;
		std::shared_ptr<boost::barrier> finishBarrier_p;
		std::shared_ptr<boost::barrier> retryBarrier_p;
		const unsigned int inc;
		unsigned int sleepTime;
      bool m_wasInAtomicAfterTrans;

   private:
      WRetryIncData& operator= (const WRetryIncData&) { return *this; }
	};

	void RetryIncAtomic(WRetryIncData::Ptr data_p, WSTM::WAtomic& at)
	{
		data_p->var_p->Set(data_p->var_p->Get(at) + data_p->inc, at);
		Wait(data_p->retryBarrier_p);
		if(0 != data_p->sleepTime)
		{
         boost::this_thread::sleep(boost::posix_time::milliseconds (data_p->sleepTime));
		}
	}

	void RetryInc(WRetryIncData::Ptr data_p)
	{
		for(unsigned int i = 0; i < data_p->repeats; ++i)
		{
			WSTM::Atomically(boost::bind(&RetryIncAtomic, data_p, _1));
         if (WSTM::InAtomic ())
         {
            data_p->m_wasInAtomicAfterTrans = true;
            break;
         }
		}
		data_p->finishBarrier_p->wait();
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retry_before)
{
	//Test with var increment coming before retry
	WRetryWaitData::Ptr waitData_p(new WRetryWaitData("test_retry_before", 10));
	waitData_p->sleepTime = 30;
	WRetryIncData::Ptr incData_p(new WRetryIncData(waitData_p, 1, 10));
	std::thread t1(boost::bind(RetryWait, waitData_p));
	std::thread t2(boost::bind(RetryInc, incData_p));
	waitData_p->finishBarrier_p->wait();
	BOOST_CHECK(!waitData_p->gotMaxRetries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(0), waitData_p->timeouts);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->retries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(10), waitData_p->var_p->GetReadOnly());
   BOOST_CHECK (!waitData_p->m_wasInAtomicAfterTrans);
   BOOST_CHECK (!incData_p->m_wasInAtomicAfterTrans);
   t1.join ();
   t2.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retry_after)
{
	//Test with var increment coming after retry
	WRetryWaitData::Ptr waitData_p(new WRetryWaitData("test_retry_after", 10));
	WRetryIncData::Ptr incData_p(new WRetryIncData(waitData_p, 1, 10));
	incData_p->sleepTime = 30;
	std::thread t1(boost::bind(RetryWait, waitData_p));
	std::thread t2(boost::bind(RetryInc, incData_p));
	waitData_p->finishBarrier_p->wait();
	BOOST_CHECK(!waitData_p->gotMaxRetries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(0), waitData_p->timeouts);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->retries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(10), waitData_p->var_p->GetReadOnly());
   BOOST_CHECK (!waitData_p->m_wasInAtomicAfterTrans);
   BOOST_CHECK (!incData_p->m_wasInAtomicAfterTrans);   

   t1.join ();
   t2.join ();
}

namespace
{
   void DoRetyTimeout (WSTM::WAtomic& at)
   {
      WSTM::Retry (at, std::chrono::milliseconds (10));
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retryTimeoutSimple)
{
   BOOST_CHECK_THROW ((WSTM::Atomically (boost::bind (DoRetyTimeout, _1))),
                      WSTM::WRetryTimeoutException);
   BOOST_CHECK (!WSTM::InAtomic ());
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retryTimeout)
{
	//test timeout in retry using timeout in WSTM::retry
	WRetryWaitData::Ptr waitData_p(new WRetryWaitData("test_retryTimeout", 20));
	waitData_p->retryTimeout = 10;
	waitData_p->retryBarrier_p.reset(new boost::barrier(2));
	WRetryIncData::Ptr incData_p(new WRetryIncData(waitData_p, 1, 10));
	incData_p->sleepTime = 30;
	std::thread t1(boost::bind(RetryWait, waitData_p));
	std::thread t2(boost::bind(RetryInc, incData_p));
	waitData_p->finishBarrier_p->wait();
	BOOST_CHECK(!waitData_p->gotMaxRetries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->timeouts);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->retries);

   t1.join ();
   t2.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retry_max_timeout)
{
   //test timeout in retry using maxRetryWait in WSTM::atomically
	WRetryWaitData::Ptr waitData_p(new WRetryWaitData("test_retry_max_timeout", 20));
	waitData_p->maxRetryWait = 10;
	waitData_p->retryBarrier_p.reset(new boost::barrier(2));
	WRetryIncData::Ptr incData_p(new WRetryIncData(waitData_p, 1, 10));
	incData_p->sleepTime = 30;
	std::thread t1(boost::bind(RetryWait, waitData_p));
	std::thread t2(boost::bind(RetryInc, incData_p));
	waitData_p->finishBarrier_p->wait();
	BOOST_CHECK(!waitData_p->gotMaxRetries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->timeouts);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(1), waitData_p->retries);

   t1.join ();
   t2.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_retry_limit)
{
   //test retry with limit hit
	WRetryWaitData::Ptr waitData_p(new WRetryWaitData("test_retry_limit", 10000));
	waitData_p->maxRetries = 5;
	waitData_p->NoRetryBarrier();
	WRetryIncData::Ptr incData_p(new WRetryIncData(waitData_p, 10, 1));
	incData_p->sleepTime = 10;
	std::thread t1(boost::bind(RetryWait, waitData_p));
	std::thread t2(boost::bind(RetryInc, incData_p));
	waitData_p->finishBarrier_p->wait();
	BOOST_CHECK(waitData_p->gotMaxRetries);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(0), waitData_p->timeouts);
	BOOST_CHECK_EQUAL(static_cast<unsigned int>(5), waitData_p->retries);

   t1.join ();
   t2.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_max_conflict_throw)
{
   const auto successValue = 428731;
   auto conflictVar = WSTM::WVar<int>(0);
   auto successVar = WSTM::WVar<int>(0);
   std::atomic<int> conflicteeCount (0);
   std::atomic<int> conflicterCount (0);
   std::atomic<int> gotExc (0);
   boost::barrier bar1 (2);
   boost::barrier bar2 (2);
   
   auto conflicteeAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicteeCount.fetch_add (1);
         if (repeat == 0)
         {
            conflictVar.Get (at);
            //signal the conflicter thread that we've read the variable
            bar1.wait ();
            successVar.Set (successValue, at);
            //wait here until the conflicter thread has committed its change to conflictVar so that
            //we will get a conflict in our transaction
            bar2.wait ();
         }
         //do nothing on the second repeat (shouldn't get a second repeat)
      };
   auto conflictee = std::thread ([&]()
                                  {
                                     try
                                     {
                                        WSTM::Atomically (conflicteeAt, WSTM::WMaxConflicts (1, WSTM::WConflictResolution::THROW));
                                     }
                                     catch(WSTM::WMaxConflictsException&)
                                     {
                                        gotExc.store (1);
                                     }
                                  });
   
   auto conflicterAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicterCount.fetch_add (1);
         if (repeat == 0)
         {
            conflictVar.Set (conflictVar.Get (at) + 1, at);
            //signal the conflictee thread that we've committed after our commit is done 
            auto& b2 = bar2;
            at.After ([&b2](){b2.wait ();});
            //wait for the conflictee thread to signal that it has read conflictVar before we commit
            bar1.wait ();
         }
         //do nothing on the second repeat
      };
   auto conflicter = std::thread ([&]()
                                  {
                                     WSTM::Atomically (conflicterAt);
                                  });
   
   conflictee.join ();
   conflicter.join ();
   
   BOOST_CHECK_EQUAL (conflicteeCount.load (), 1);
   BOOST_CHECK_EQUAL (conflicterCount.load (), 1);
   BOOST_CHECK_EQUAL (successVar.GetReadOnly (), 0);
   BOOST_CHECK_EQUAL (gotExc.load (), 1);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_max_conflict_lock)
{
   const auto successValue = 428731;
   auto conflictVar = WSTM::WVar<int>(0);
   auto successVar = WSTM::WVar<int>(0);
   std::atomic<int> conflicteeCount (0);
   std::atomic<int> conflicterCount (0);
   boost::barrier bar1 (2);
   boost::barrier bar2 (2);
   
   auto conflicteeAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicteeCount.fetch_add (1);
         if (repeat < 2)
         {
            conflictVar.Get (at);
            //signal the conflicter thread that we've read the variable
            bar1.wait ();
            successVar.Set (successValue, at);
            //wait here until the conflicter thread has committed its change to conflictVar so that
            //we will get a conflict in our transaction
            bar2.wait ();
            if (repeat == 1)
            {
               //give the other thread some time to get ahead of us and get blocked on the commit
               //lock that we should be holding
               boost::this_thread::sleep (boost::posix_time::milliseconds (200));
            }
         }
         //do nothing on the third repeat (shouldn't get a thrid repeat)
      };
   auto conflictee = std::thread ([&](){WSTM::Atomically (conflicteeAt, WSTM::WMaxConflicts (1, WSTM::WConflictResolution::RUN_LOCKED));});

   auto conflicterAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicterCount.fetch_add (1);
         if (repeat < 2)
         {
            conflictVar.Set (conflictVar.Get (at) + 1, at);
            auto& b2 = bar2;
            if (repeat == 0)
            {
               //signal the conflictee thread that we've committed after our commit is done 
               at.After ([&b2](){b2.wait ();});
            }
            else if (repeat == 1)
            {
               //signal the conflictee thread that we're committing before we start our commit (we
               //should get blocked on the commit lock until the conflictee commits this time so we
               //can't signal after our commit is done) 
               at.BeforeCommit ([&b2](WSTM::WAtomic&){b2.wait ();});
            }
            //wait for the conflictee thread to signal that it has read conflictVar before we commit
            bar1.wait ();
         }
         //do nothing on the third repeat
      };
   auto conflicter = std::thread ([&]()
                                  {
                                     for (auto i = 0; i < 2; ++i)
                                     {
                                        WSTM::Atomically (conflicterAt);
                                     }
                                  });

   conflictee.join ();
   conflicter.join ();

   BOOST_CHECK_EQUAL (conflicteeCount.load (), 2);
   BOOST_CHECK_EQUAL (conflicterCount.load (), 2);
   BOOST_CHECK_EQUAL (successVar.GetReadOnly (), successValue);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_max_conflict_lock_with_sub_trans)
{
   //we had problems with sub-transactions cancelling the commit lock of parent transactions that
   //were running locked, this test makes sure that the problem has not come back
   
   const auto successValue = 428731;
   auto conflictVar = WSTM::WVar<int>(0);
   auto successVar = WSTM::WVar<int>(0);
   auto extraVar = WSTM::WVar<int>(0);
   std::atomic<int> conflicteeCount (0);
   std::atomic<int> conflicterCount (0);
   boost::barrier bar1 (2);
   boost::barrier bar2 (2);
   
   auto conflicteeAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicteeCount.fetch_add (1);
         if (repeat < 2)
         {
            conflictVar.Get (at);
            //signal the conflicter thread that we've read the variable
            bar1.wait ();
            successVar.Set (successValue, at);
            if (repeat == 1)
            {
               //this Set will start a sub-transaction, we had problems with sub-transactions
               //cancelling the commit lock of parent transactions that were running locked (which
               //this transaction should be at this point)
               extraVar.Set (extraVar.Get (at) + 1);
            }
            //wait here until the conflicter thread has committed its change to conflictVar so that
            //we will get a conflict in our transaction
            bar2.wait ();
            if (repeat == 1)
            {
               //give the other thread some time to get ahead of us and get blocked on the commit
               //lock that we should be holding
               boost::this_thread::sleep (boost::posix_time::milliseconds (200));
            }
         }
         //do nothing on the third repeat (shouldn't get a thrid repeat)
      };
   auto conflictee = std::thread ([&](){WSTM::Atomically (conflicteeAt, WSTM::WMaxConflicts (1, WSTM::WConflictResolution::RUN_LOCKED));});

   auto conflicterAt =
      [&](WSTM::WAtomic& at)
      {
         const auto repeat = conflicterCount.fetch_add (1);
         if (repeat < 2)
         {
            conflictVar.Set (conflictVar.Get (at) + 1, at);
            auto& b2 = bar2;
            if (repeat == 0)
            {
               //signal the conflictee thread that we've committed after our commit is done 
               at.After ([&b2](){b2.wait ();});
            }
            else if (repeat == 1)
            {
               //signal the conflictee thread that we're committing before we start our commit (we
               //should get blocked on the commit lock until the conflictee commits this time so we
               //can't signal after our commit is done) 
               at.BeforeCommit ([&b2](WSTM::WAtomic&){b2.wait ();});
            }
            //wait for the conflictee thread to signal that it has read conflictVar before we commit
            bar1.wait ();
         }
         //do nothing on the third repeat
      };
   auto conflicter = std::thread ([&]()
                                  {
                                     for (auto i = 0; i < 2; ++i)
                                     {
                                        WSTM::Atomically (conflicterAt);
                                     }
                                  });

   conflictee.join ();
   conflicter.join ();

   BOOST_CHECK_EQUAL (conflicteeCount.load (), 2);
   BOOST_CHECK_EQUAL (conflicterCount.load (), 2);
   BOOST_CHECK_EQUAL (successVar.GetReadOnly (), successValue);
}

namespace
{
	struct WTestException
	{};

	void DoThrowTest(WSTM::WVar<int>& var, WSTM::WAtomic& at)
	{
		var.Set(var.Get(at) + 1, at);
		throw WTestException();
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_op_throws)
{
	const int val = 10;
	WSTM::WVar<int> var(val);
	bool gotExc = false;
	try
	{
		WSTM::Atomically(boost::bind(DoThrowTest, boost::ref(var), _1));
	}
	catch(WTestException&)
	{
		gotExc = true;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(val, var.GetReadOnly());
}

namespace
{
	const int nestedVal = 101;
	const int toplevelVal = 200;

	void Nested(WSTM::WVar<int>& var, bool& sawGoodValue, WSTM::WAtomic& at)
	{
		sawGoodValue = (toplevelVal == var.Get(at));
		var.Set(nestedVal, at);
		throw WTestException();
	}

	bool Toplevel(WSTM::WVar<int>& var, bool& sawGoodValue, WSTM::WAtomic& at)
	{
		var.Set(toplevelVal, at);
		try
		{
			WSTM::Atomically(boost::bind(Nested, boost::ref(var), boost::ref(sawGoodValue), _1));
		}
		catch(WTestException&)
		{
			return true;
		}

		return false;
	}
	
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_transaction_rollback)
{
	const int init_val = 1;
	WSTM::WVar<int> var(init_val);
	bool nestedSawGoodValue;
	bool gotExc = WSTM::Atomically(boost::bind(Toplevel,
                                              boost::ref(var),
                                              boost::ref(nestedSawGoodValue),
                                              _1));
   BOOST_CHECK(gotExc);
	BOOST_CHECK(nestedSawGoodValue);
	BOOST_CHECK_EQUAL(toplevelVal, var.GetReadOnly());
}

namespace
{
	void TestDisappearingVarAtomic(WSTM::WAtomic& at)
	{
		std::shared_ptr<WSTM::WVar<int> > var_p(new WSTM::WVar<int>(0));
		var_p->Get(at);
		var_p.reset();
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_disappearing_var)
{
	WSTM::Atomically(boost::bind(TestDisappearingVarAtomic, _1));
	//if we crash before getting here it means that STM is not dealing
	//with variables being deleted during transactions
   BOOST_CHECK (true);
}

namespace TestRefReturn
{
	int refVal = 3598798;

	int& GetRefVal(WSTM::WAtomic& /*at*/)
	{
		return refVal;
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_ref_return)
{
	//Make sure that WSTM::atomically deals with functions that return
	//references properly.
	int& result = WSTM::Atomically(boost::bind(TestRefReturn::GetRefVal, _1));
	BOOST_CHECK_EQUAL(&TestRefReturn::refVal, &result);
}

namespace TestNestedGet
{
	const int gotVarVal = 395879;
	WSTM::WVar<int> gotVar(gotVarVal);

	const int setVarInitVal = 45987;
	const int setVarSetVal = 89475;	
	WSTM::WVar<int> setVar(setVarInitVal);
	
	void child(WSTM::WAtomic& at)
	{
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarSetVal, setVar.Get(at));

		//Check value again to make sure that saving values from
		//parent in child transaction didn't screw up
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarSetVal, setVar.Get(at));
	}

	void parent(WSTM::WAtomic& at)
	{
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarInitVal, setVar.Get(at));		
		setVar.Set(setVarSetVal, at);
		WSTM::Atomically(boost::bind(TestNestedGet::child, _1));
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarSetVal, setVar.Get(at));		
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_get)
{
	//Make sure that getting a variable that has been gotten or set in
	//the parent transaction works
	WSTM::Atomically(boost::bind(TestNestedGet::parent, _1));
}

namespace TestNestedSet
{
	const int gotVarVal = 134;
	WSTM::WVar<int> gotVar(gotVarVal);

	const int setVarInitVal = 974;
	const int setVarSetVal = 98346;	
	WSTM::WVar<int> setVar(setVarInitVal);
	
	void child(WSTM::WAtomic& at)
	{
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarInitVal, setVar.Get(at));		
		setVar.Set(setVarSetVal, at);

		//Check value again to make sure that saving values from
		//parent in child transaction didn't screw up
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarSetVal, setVar.Get(at));		
	}

	void parent(WSTM::WAtomic& at)
	{
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarInitVal, setVar.Get(at));		
		WSTM::Atomically(boost::bind(TestNestedSet::child, _1));
		BOOST_CHECK_EQUAL(gotVarVal, gotVar.Get(at));
		BOOST_CHECK_EQUAL(setVarSetVal, setVar.Get(at));		
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_set)
{
	//Make sure that setting a variable that has been gotten or set in
	//the parent transaction works
	WSTM::Atomically(boost::bind(TestNestedSet::parent, _1));
	BOOST_CHECK_EQUAL(TestNestedSet::gotVarVal,
                     TestNestedSet::gotVar.GetReadOnly());
	BOOST_CHECK_EQUAL(TestNestedSet::setVarSetVal,
                     TestNestedSet::setVar.GetReadOnly());		
}

namespace
{
	std::pair<int, int> TestInternalConstReturnAtomic(WSTM::WAtomic&)
	{
		return std::pair<int, int>(1, 2);
	}
	
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_internal_const_return)
{
	std::pair<int, int> result =
		WSTM::Atomically(boost::bind(TestInternalConstReturnAtomic, _1));
	BOOST_CHECK_EQUAL(1, result.first);
	BOOST_CHECK_EQUAL(2, result.second);
}

namespace
{
	int commitHookNumCalls = 0;
	
	struct WIncrementNumCalls
	{
		void operator()() const
		{
			BOOST_CHECK(!WSTM::InAtomic());
			++commitHookNumCalls;
		}
	};

	void TestCommitHookAtomic(WSTM::WAtomic& at)
	{
		at.After(WIncrementNumCalls());
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_commit_hook)
{
	commitHookNumCalls = 0;
	WSTM::Atomically(boost::bind(TestCommitHookAtomic, _1));
	BOOST_CHECK_EQUAL(1, commitHookNumCalls);
}

namespace
{
	void TestCommitHookNestedNoHookAtomic(WSTM::WAtomic& /*at*/)
	{}
	
	void TestCommitHookNestedAtomic(WSTM::WAtomic& at, bool topHook, bool childHook)
	{
		if(childHook)
		{
			WSTM::Atomically(boost::bind(TestCommitHookAtomic, _1));
			BOOST_CHECK_EQUAL(0, commitHookNumCalls);
		}
		else
		{
			WSTM::Atomically(boost::bind(TestCommitHookNestedNoHookAtomic, _1));
		}
		if(topHook)
		{
			at.After(WIncrementNumCalls());
		}
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_commit_hook_nested)
{
	//test with no hooks.
	commitHookNumCalls = 0;
	WSTM::Atomically(boost::bind(TestCommitHookNestedAtomic, _1, false, false));
	BOOST_CHECK_EQUAL(0, commitHookNumCalls);

	//test with hook at top level
	commitHookNumCalls = 0;
	WSTM::Atomically(boost::bind(TestCommitHookNestedAtomic, _1, true, false));
	BOOST_CHECK_EQUAL(1, commitHookNumCalls);

	//test with hook coming from child transaction.
	commitHookNumCalls = 0;
	WSTM::Atomically(boost::bind(TestCommitHookNestedAtomic, _1, false, true));
	BOOST_CHECK_EQUAL(1, commitHookNumCalls);

	//test with hook coming from top-level and child.
	commitHookNumCalls = 0;
	WSTM::Atomically(boost::bind(TestCommitHookNestedAtomic, _1, true, true));
	BOOST_CHECK_EQUAL(2, commitHookNumCalls);
}

namespace CommitHookConflict
{
	WSTM::WVar<int> var(0);
	boost::barrier conflictBarrier(2);
	int conflicterCount = 0;
	
	void Conflicter(WSTM::WAtomic& at)
	{
		conflictBarrier.wait();
		var.Set(var.Get(at) + 1, at);
	}

	void ConflicterThread()
	{
		while(conflicterCount > 0)
		{
			WSTM::Atomically(boost::bind(Conflicter, _1));
			conflictBarrier.wait();
			--conflicterCount;
		}

		//needs these waits so that conflictee gets
		//thorugh the final time.
		conflictBarrier.wait();
		conflictBarrier.wait();
	}

	void Conflictee(WSTM::WAtomic& at, bool topHook, bool childHook)
	{
		var.Get(at);
		conflictBarrier.wait();
						
		if(childHook)
		{
			WSTM::Atomically(boost::bind(TestCommitHookAtomic, _1));
		}

		if(topHook)
		{
			at.After(WIncrementNumCalls());
		}
		conflictBarrier.wait();
	}
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_commit_hook_conflict)
{
	//test with no commit hooks
	commitHookNumCalls = 0;
	CommitHookConflict::conflicterCount = 1;
	std::thread t1(boost::bind(CommitHookConflict::ConflicterThread));
	WSTM::Atomically(boost::bind(CommitHookConflict::Conflictee, _1, false, false));
	t1.join();
	BOOST_CHECK_EQUAL(0, commitHookNumCalls);
					
	//test with one top-level commit hook
	commitHookNumCalls = 0;
	CommitHookConflict::conflicterCount = 2;
	std::thread t2(boost::bind(CommitHookConflict::ConflicterThread));
	WSTM::Atomically(boost::bind(CommitHookConflict::Conflictee, _1, true, false));
	t2.join();
	BOOST_CHECK_EQUAL(1, commitHookNumCalls);

	//test with a commit hook coming from the child transaction
	commitHookNumCalls = 0;
	CommitHookConflict::conflicterCount = 3;
	std::thread t3(boost::bind(CommitHookConflict::ConflicterThread));
	WSTM::Atomically(boost::bind(CommitHookConflict::Conflictee, _1, false, true));
	t3.join();
	BOOST_CHECK_EQUAL(1, commitHookNumCalls);

	//test with commit hooks comign at the top-level and the child
	//level. 
	commitHookNumCalls = 0;
	CommitHookConflict::conflicterCount = 4;
	std::thread t4(boost::bind(CommitHookConflict::ConflicterThread));
	WSTM::Atomically(boost::bind(CommitHookConflict::Conflictee, _1, true, true));
	t4.join();
	BOOST_CHECK_EQUAL(2, commitHookNumCalls);

	//test with the commit hook coming from the transaction running locked.
	commitHookNumCalls = 0;
	CommitHookConflict::conflicterCount = 1;
	std::thread t5(boost::bind(CommitHookConflict::ConflicterThread));
	WSTM::Atomically(boost::bind(CommitHookConflict::Conflictee, _1, true, true),
                    WSTM::WMaxConflicts(1, WSTM::WConflictResolution::RUN_LOCKED));
	t5.join();
	BOOST_CHECK_EQUAL(2, commitHookNumCalls);
}

namespace
{
	struct WCommitHookAtomic
	{
		typedef void result_type;
						
		WCommitHookAtomic() : ran(false) {}

		void operator()(WSTM::WAtomic& at)
		{
			at.After(boost::bind(&WCommitHookAtomic::PostCommit, this));
		}
						
		void PostCommit()
		{
			WSTM::Atomically(boost::bind(&WCommitHookAtomic::RunAtomic, this, _1));
		}

		void RunAtomic(WSTM::WAtomic& /*at*/)
		{
			ran = true;
		}
						
		bool ran;
	};
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_commit_hook_uses_atomic)
{
	WCommitHookAtomic hook;
	BOOST_CHECK_NO_THROW(WSTM::Atomically(boost::bind(boost::ref(hook), _1)));
	BOOST_CHECK(hook.ran);
}
				
BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_transaction_retry)
{
	struct WRetryThread
	{
		WRetryThread() : timedout(false), m_firstTime(true) {}
		bool timedout;
		bool m_firstTime;
						
		void Run(barrier& bar, WSTM::WVar<bool>& var)
		{
			try
			{
				WSTM::Atomically(boost::bind(&WRetryThread::RunAtomic,
                                         this,
                                         boost::ref(bar),
                                         boost::ref(var),
                                         _1));
			}
			catch(WSTM::WRetryTimeoutException&)
			{
				timedout = true;
			}
		}

		void RunAtomic(barrier& bar, WSTM::WVar<bool>& var, WSTM::WAtomic& /*at*/)
		{
			WSTM::Atomically(boost::bind(&WRetryThread::RunAtomicNested,
                                      this,
                                      boost::ref(bar),
                                      boost::ref(var),
                                      _1));
		}

		void RunAtomicNested(barrier& bar, WSTM::WVar<bool>& var, WSTM::WAtomic& at)
		{
			const bool val = var.Get(at);
			if(m_firstTime)
			{
				bar.wait();
				bar.wait();
				m_firstTime = false;
			}
			if(!val)
			{
				WSTM::Retry(at, std::chrono::milliseconds (1000));
			}
		}
						
	};

	//test var update between get and retry
	WRetryThread ret;
	barrier bar(2);
	WSTM::WVar<bool> var(false);
   std::thread thr(boost::bind(&WRetryThread::Run, boost::ref(ret), boost::ref(bar), boost::ref(var)));
	bar.wait();
   var.Set (true);
	bar.wait();
	thr.join();
	BOOST_CHECK(!ret.timedout);

	//test var update after get and retry
	WSTM::Atomically(boost::bind(&WSTM::WVar<bool>::Set, boost::ref(var), false, _1));
	ret.m_firstTime = true;
   std::thread thr2(boost::bind(&WRetryThread::Run, boost::ref(ret), boost::ref(bar), boost::ref(var)));
	bar.wait();
	bar.wait();
   boost::this_thread::sleep(boost::posix_time::milliseconds (50));
	WSTM::Atomically(boost::bind(&WSTM::WVar<bool>::Set, boost::ref(var), true, _1));
	thr2.join();
	BOOST_CHECK(!ret.timedout);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_transaction_parent_retry)
{
	//tests retrying a transaction when the variable
	//change is made in a child transaction of a
	//different transaction. Watches for a bug where
	//the change signals from the child-transaction
	//were not being emitted when the parent
	//transaction commits.

	struct WRetryThread
	{
		WRetryThread() : timedout(false), m_firstTime(true) {}
		bool timedout;
		bool m_firstTime;
						
		void Run(barrier& bar, WSTM::WVar<bool>& var)
		{
			try
			{
				WSTM::Atomically(boost::bind(&WRetryThread::RunAtomic,
                                         this,
                                         boost::ref(bar),
                                         boost::ref(var),
                                         _1));
			}
			catch(WSTM::WRetryTimeoutException&)
			{
				timedout = true;
			}
		}

		void RunAtomic(barrier& bar, WSTM::WVar<bool>& var, WSTM::WAtomic& at)
		{
			const bool val = var.Get(at);
			if(m_firstTime)
			{
				bar.wait();
				bar.wait();
				m_firstTime = false;
			}
			if(!val)
			{
				WSTM::Retry(at, std::chrono::milliseconds (100));
			}
		}
	};

	struct SetVar
	{
		void Run(WSTM::WVar<bool>& var, WSTM::WAtomic& /*at*/)
		{
			WSTM::Atomically(boost::bind(&SetVar::RunNested, this, boost::ref(var), _1));
		}
						
		void RunNested(WSTM::WVar<bool>& var, WSTM::WAtomic& at)
		{
			var.Set(true, at);
		}
	};

	//test var update between get and retry
	WRetryThread ret;
	barrier bar(2);
	WSTM::WVar<bool> var(false);
   std::thread thr(boost::bind(&WRetryThread::Run, boost::ref(ret), boost::ref(bar), boost::ref(var)));
	bar.wait();
   SetVar sv1;
	WSTM::Atomically(boost::bind(&SetVar::Run, boost::ref (sv1), boost::ref(var), _1));
	bar.wait();
	thr.join();
	BOOST_CHECK(!ret.timedout);

	//test var update after get and retry
	WSTM::Atomically(boost::bind(&WSTM::WVar<bool>::Set, boost::ref(var), false, _1));
	ret.m_firstTime = true;
   std::thread thr2(boost::bind(&WRetryThread::Run, boost::ref(ret), boost::ref(bar), boost::ref(var)));
	bar.wait();
	bar.wait();
   boost::this_thread::sleep(boost::posix_time::milliseconds (50));
   SetVar sv2;
	WSTM::Atomically(boost::bind(&SetVar::Run, boost::ref (sv2), boost::ref(var), _1));
	thr2.join();
	BOOST_CHECK(!ret.timedout);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_inconsistent)
{
	static const int val1 = 758519;
	struct GetValueVoid
	{
		static void Run(WSTM::WVar<int>& v, WSTM::WInconsistent& ins)
		{
			BOOST_CHECK_NO_THROW(v.GetInconsistent(ins));
			BOOST_CHECK_EQUAL(val1, v.GetInconsistent(ins));
		}
	};
   WSTM::WVar<int> v1(val1);
	BOOST_CHECK_NO_THROW(WSTM::Inconsistently(boost::bind(GetValueVoid::Run, boost::ref(v1), _1)));

	static const int val2 = 894935;
	struct GetValue
	{
		static int Run(WSTM::WVar<int>& v, WSTM::WInconsistent& ins)
		{
			BOOST_CHECK_NO_THROW(v.GetInconsistent(ins));
			BOOST_CHECK_EQUAL(val2, v.GetInconsistent(ins));
			return val2 + 1;
		}
	};
   WSTM::WVar<int> v2(val2);
	int res = 0;
	BOOST_CHECK_NO_THROW(
		res = WSTM::Inconsistently(boost::bind(GetValue::Run, boost::ref(v2), _1)));
	BOOST_CHECK_EQUAL(val2 + 1, res);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_inconsistentInAtomic)
{
	struct RunInconsistent
	{
		static void Run(WSTM::WInconsistent&)
		{
			BOOST_FAIL("Inconsistent within atomic");
		}
	};

	struct RunAtomic
	{
		static void Run(WSTM::WAtomic& /*at*/)
		{
         WSTM::Inconsistently (boost::bind (RunInconsistent::Run, _1));
		}
	};

	BOOST_CHECK_THROW(WSTM::Atomically(boost::bind(RunAtomic::Run, _1)),
                     WSTM::WInAtomicError);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_inconsistentConflict)
{
	struct RunInconsistent
	{
		static int Run(boost::barrier& b,
                     int& numRuns,
                     WSTM::WVar<int>& v,
                     WSTM::WInconsistent& ins)
		{
			++numRuns;
			const int val = v.GetInconsistent(ins);
			b.wait();
			b.wait();
			return val + 1;
		}
	};

	struct RunAtomic
	{
		static void Run(boost::barrier& b,
                      WSTM::WVar<int>& v,
                      WSTM::WAtomic& at)
		{
			b.wait();
			v.Set(v.Get(at) + 1, at);
			at.After(boost::bind(&boost::barrier::wait, boost::ref(b)));
		}

		static void Start(boost::barrier& b, WSTM::WVar<int>& v)
		{
			Atomically(boost::bind(Run, boost::ref(b), boost::ref(v), _1));
		}
						
	};

	const int val = 75102;
   WSTM::WVar<int> v(val);
	boost::barrier b(2);
	std::thread t(boost::bind(RunAtomic::Start, boost::ref(b), boost::ref(v)));
	int numRuns = 0;
	const int res =
		WSTM::Inconsistently(boost::bind(RunInconsistent::Run,
                                       boost::ref(b),
                                       boost::ref(numRuns),
                                       boost::ref(v),
                                       _1));
	t.join();
	BOOST_CHECK_EQUAL(1, numRuns);
	BOOST_CHECK_EQUAL(val + 1, res);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_ReadLockAtomic)
{
	struct Update
	{
		static void Run(WSTM::WVar<int>& v, boost::barrier& b, WSTM::WAtomic& at)
		{
			b.wait();
			v.Set(v.Get(at) + 1, at);
		}

		static void Start(WSTM::WVar<int>& v, boost::barrier& b)
		{
			WSTM::Atomically(boost::bind(Update::Run, boost::ref(v), boost::ref(b), _1));
		}
	};

	struct Read
	{
		static void Run(WSTM::WVar<int>& v,
                      const int oldVal,
                      boost::barrier& b,
                      int& runs,
                      WSTM::WAtomic& at)
		{
			at.ReadLock();
			if(runs == 0)
			{
				b.wait();
				//sleep for a while so that if the other
				//thread was unconstarined it would comit
            boost::this_thread::sleep(boost::posix_time::milliseconds (50));
			}
			const int val = v.Get(at);
			at.ReadUnlock();
			BOOST_CHECK_EQUAL(oldVal + runs, val);
			++runs;
		}
	};

	const int init = 555968;
	WSTM::WVar<int> v(init);
	boost::barrier b(2);
	std::thread t(boost::bind(Update::Start, boost::ref(v), boost::ref(b)));
	int runs = 0;
	WSTM::Atomically(boost::bind(Read::Run,
                                boost::ref(v),
                                init,
                                boost::ref(b),
                                boost::ref(runs),
                                _1));
	t.join();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_ReadLockInconsistent)
{
	struct Update
	{
		static void Run(WSTM::WVar<int>& v, boost::barrier& b, WSTM::WAtomic& at)
		{
			b.wait();
			v.Set(v.Get(at) + 1, at);
		}

		static void Start(WSTM::WVar<int>& v, boost::barrier& b)
		{
			WSTM::Atomically(boost::bind(Update::Run, boost::ref(v), boost::ref(b), _1));
		}
	};

	struct Read
	{
		static void Run(WSTM::WVar<int>& v,
                      const int oldVal,
                      boost::barrier& b,
                      WSTM::WInconsistent& inc)
		{
			inc.ReadLock();
			b.wait();
			//sleep for a while so that if the other
			//thread was unconstarined it would comit
         boost::this_thread::sleep(boost::posix_time::milliseconds (50));
			const int val = v.GetInconsistent(inc);
			inc.ReadUnlock();
			BOOST_CHECK_EQUAL(oldVal, val);
		}
	};

   WSTM::InAtomic ();
	const int init = 106226;
	WSTM::WVar<int> v(init);
   WSTM::InAtomic ();
	boost::barrier b(2);
   WSTM::InAtomic ();
	std::thread t(boost::bind(Update::Start, boost::ref(v), boost::ref(b)));
   WSTM::InAtomic ();
	WSTM::Inconsistently(boost::bind(Read::Run, boost::ref(v), init, boost::ref(b), _1));
	t.join();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_ReadLockRetry)
{
	struct Update
	{
		static void Run(WSTM::WVar<int>& v, boost::barrier& b, WSTM::WAtomic& at)
		{
			b.wait();
         boost::this_thread::sleep(boost::posix_time::milliseconds (50));
			v.Set(v.Get(at) + 1, at);
		}

		static void Start(WSTM::WVar<int>& v, boost::barrier& b)
		{
			WSTM::Atomically(boost::bind(Update::Run, boost::ref(v), boost::ref(b), _1));
		}
	};
					
	struct Read
	{
		static void Run(WSTM::WVar<int>& v,
                      const int oldVal,
                      boost::barrier& b,
                      int& runs,
                      WSTM::WAtomic& at)
		{
			at.ReadLock();
			const int val = v.Get(at);
			if(runs == 0)
			{
				b.wait();
				++runs;
				//if things are working properly then
				//we should not get a deadlock here
				WSTM::Retry(at);
			}
			at.ReadUnlock();
			BOOST_CHECK_EQUAL(oldVal + runs, val);
		}
	};

	const int init = 664040;
	WSTM::WVar<int> v(init);
	boost::barrier b(2);
	std::thread t(boost::bind(Update::Start, boost::ref(v), boost::ref(b)));
	int runs = 0;
	WSTM::Atomically(boost::bind(Read::Run,
                                boost::ref(v),
                                init,
                                boost::ref(b),
                                boost::ref(runs),
                                _1));
	t.join();
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_ReadLockIsLocked)
{
	struct WTest
	{
		static void RunAtomic(WSTM::WAtomic& at)
		{
			BOOST_CHECK_NO_THROW(at.ReadLock()); // lock count = 1
			BOOST_CHECK(at.IsReadLocked());
			BOOST_CHECK_NO_THROW(at.ReadLock()); // lock count = 2
			BOOST_CHECK(at.IsReadLocked());
			BOOST_CHECK_NO_THROW(at.ReadUnlock()); // lock count = 1
			BOOST_CHECK(at.IsReadLocked());
			BOOST_CHECK_NO_THROW(at.ReadUnlock()); // lock count = 0
			BOOST_CHECK(!at.IsReadLocked());
			BOOST_CHECK_NO_THROW(at.ReadUnlock()); // no effect
			BOOST_CHECK(!at.IsReadLocked());

			{
				WSTM::WReadLockGuard<WSTM::WAtomic> lock(at);
				BOOST_CHECK(at.IsReadLocked());
			}
			BOOST_CHECK(!at.IsReadLocked());
		}

		static void RunInconsistent(WSTM::WInconsistent& inc)
		{
			BOOST_CHECK_NO_THROW(inc.ReadLock()); // lock count = 1
			BOOST_CHECK(inc.IsReadLocked());
			BOOST_CHECK_NO_THROW(inc.ReadLock()); // lock count = 2
			BOOST_CHECK(inc.IsReadLocked());
			BOOST_CHECK_NO_THROW(inc.ReadUnlock()); // lock count = 1
			BOOST_CHECK(inc.IsReadLocked());
			BOOST_CHECK_NO_THROW(inc.ReadUnlock()); // lock count = 0
			BOOST_CHECK(!inc.IsReadLocked());
			BOOST_CHECK_NO_THROW(inc.ReadUnlock()); //no effect
			BOOST_CHECK(!inc.IsReadLocked());

			{
				WSTM::WReadLockGuard<WSTM::WInconsistent> lock(inc);
				BOOST_CHECK(inc.IsReadLocked());
			}
			BOOST_CHECK(!inc.IsReadLocked());
		}
	};
	WSTM::Atomically(boost::bind(WTest::RunAtomic, _1));
	WSTM::Inconsistently(boost::bind(WTest::RunInconsistent, _1));					
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_get_split_transaction_levels)
{
	//This makes sure the committing works correctly
	//when a variable is gotten at two non-consecutive
	//transaction levels
	struct Subs
	{
		static void Run(WSTM::WVar<int>& v, const int maxLvl, const int lvl, WSTM::WAtomic& at)
		{
			if(lvl == 1 || lvl == maxLvl)
			{
				v.Get(at);
			}
			if(lvl < maxLvl)
			{
				WSTM::Atomically(boost::bind(Subs::Run, boost::ref(v), maxLvl, lvl + 1, _1));
			}
		}
	};
	WSTM::WVar<int> v(0);
	for(int maxLvl = 3; maxLvl < 6; ++ maxLvl)
	{
		//an assertion inside of Var::commit will fail
		//if there is a problem
		WSTM::Atomically(boost::bind(Subs::Run, boost::ref(v), maxLvl, 1, _1));
	}

   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_set_split_transaction_levels)
{
	//This makes sure the committing works correctly
	//when a variable is set at two non-consecutive
	//transaction levels
	struct Subs
	{
		static void Run(WSTM::WVar<int>& v, const int maxLvl, const int lvl, WSTM::WAtomic& at)
		{
			if(lvl == 1 || lvl == maxLvl)
			{
				v.Set(lvl, at);
			}
			if(lvl < maxLvl)
			{
				WSTM::Atomically(boost::bind(Subs::Run, boost::ref(v), maxLvl, lvl + 1, _1));
			}
		}
	};
	WSTM::WVar<int> v(0);
	for(int maxLvl = 3; maxLvl < 6; ++ maxLvl)
	{
		//an assertion inside of Var::commit will fail
		//if there is a problem
		WSTM::Atomically(boost::bind(Subs::Run, boost::ref(v), maxLvl, 1, _1));
	}

   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (StmVarTests_test_nested_validation_fail)
{
	//This makes sure that validation failures in a
	//nested transaction behave properly. At one point
	//when a variable was read in the top-level and a
	//nested transaction the failure of the nested
	//transaction would cause an infinite loop.
	struct GetVar
	{
		static void Run(WSTM::WVar<int>& v, const int lvl, boost::barrier& b, WSTM::WAtomic& at)
		{
			v.Get(at);
			if(lvl == 1)
			{
				b.wait();
				b.wait();
			}
			if(lvl < 2)
			{
				Atomically(boost::bind(Run, boost::ref(v), lvl + 1, boost::ref(b),  _1));
			}
		}
	};
	struct OtherThread
	{
		static void Run(WSTM::WVar<int>& v, boost::barrier& b)
		{
			Atomically(boost::bind(GetVar::Run, boost::ref(v), 1, boost::ref(b), _1));
		}
	};
	boost::barrier b(2);
	WSTM::WVar<int> v(0);
	std::thread t(boost::bind(OtherThread::Run, boost::ref(v), boost::ref(b)));
	b.wait();
	//other thread has got var
   v.Set (1);
	b.wait();
	//other thread will now try nested transaction
	b.wait();
	b.wait();
	//have to wait two more times due to retry of transaction
	t.join();
	//we will never reach this point if the bug still
	//exists
   BOOST_CHECK (true);
}

namespace
{
   struct WAtomicallyInDtor
   {
      int m_value;
      WSTM::WVar<int> m_var;
      WAtomicallyInDtor ();
      WAtomicallyInDtor (const WAtomicallyInDtor& other);
      WAtomicallyInDtor& operator=(const WAtomicallyInDtor& other);
      ~WAtomicallyInDtor ();
   };

   WAtomicallyInDtor::WAtomicallyInDtor ():
      m_value (0),
      m_var (0)
   {}

   WAtomicallyInDtor::WAtomicallyInDtor (const WAtomicallyInDtor& other) :
      m_value (other.m_value),
      m_var (other.m_var.GetReadOnly ())
   {}

   WAtomicallyInDtor& WAtomicallyInDtor::operator=(const WAtomicallyInDtor& other)
   {
      m_value = other.m_value;
      m_var.Set (other.m_var.GetReadOnly ());
      return *this;
   }

   void UpdateValue (WSTM::WVar<WAtomicallyInDtor>& var, WSTM::WAtomic& at)
   {
      const WAtomicallyInDtor& old = var.Get (at);
      WAtomicallyInDtor newOne (old);
      ++newOne.m_value;
      var.Set (newOne, at);
   }
   
   WAtomicallyInDtor::~WAtomicallyInDtor ()
   {
      const int val = WSTM::Atomically (boost::bind (&WSTM::WVar<int>::Get, boost::ref (m_var), _1));
      WSTM::Atomically (boost::bind (&WSTM::WVar<int>::Set, boost::ref (m_var), val + 1, _1));
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_AtomicallyInValueDtor)
{
   //This tests the use of Atomically in the dtor of a value stored in
   //a WVar (there used to be a problem with doing this)
   WSTM::WVar<WAtomicallyInDtor> var;
   WSTM::Atomically (boost::bind (UpdateValue, boost::ref (var), _1));
   //If we got here with no assertions then the test has passed
   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (StmVarTests_AtomicallyDuringThreadExit)
{
   //This tests Atomically being called during thread exit (this can
   //happen if it is called by a destructor of an object that is bound
   //into the thread function). This caused memory corruption and/or a
   //crash in the past.

   struct WThreadFunc
   {
      std::shared_ptr<barrier> m_bar_p;
      bool m_run;
      
      WThreadFunc (const std::shared_ptr<barrier>& bar_p):
         m_bar_p (bar_p),
         m_run (false)
      {}

      static void DuringThreadExit (WSTM::WAtomic& /*at*/)
      {
         //does nothing, calling atomically will cause a failure
         //before we get here
      }

      void operator()()
      {
         m_bar_p->wait ();

         //run a transaction so that the STM system sets up its thread
         //cleanup function
         WSTM::WVar<int> v (0);
         v.Set (1);

         m_run = true;
      }

      ~WThreadFunc ()
      {
         if (m_run)
         {
            WSTM::Atomically (boost::bind (DuringThreadExit, _1));
            m_bar_p->wait ();
         }
      }
      
   };

   std::shared_ptr<barrier> bar_p (new barrier (2));
   {
      //scope introduced so that the thread object will be gone by the
      //time the WThreadFunc dtor runs, otherwise we don't get the
      //right effect and miss any memory corruption
      std::thread (WThreadFunc (bar_p)).detach ();
   }
   bar_p->wait ();
   bar_p->wait ();   

   //If we got here with no assertions then the test has passed
   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (single_var_validation)
{
   auto var1 = WSTM::WVar<bool>(false);
   auto var2 = WSTM::WVar<bool>(false);
   auto preCounter = 0;
   auto postCounter = 0;
   WSTM::Atomically ([&](WSTM::WAtomic& at)
                     {
                        ++preCounter;
                        var1.Get (at);
                        var2.Get (at);
                        var1.Validate (at);
                        var2.Validate (at);
                        ++postCounter;
                     });
   //with no conflict we should sail straight through
   BOOST_CHECK_EQUAL (preCounter, 1);
   BOOST_CHECK_EQUAL (postCounter, 1);

   preCounter = 0;
   postCounter = 0;
   auto firstTime = true;
   auto gotConflict = false;
   WSTM::Atomically ([&](WSTM::WAtomic& at)
                     {
                        if (!firstTime)
                        {
                           //we shouldn't get a conflict, bail here if we do so we don't get in an
                           //infinite conflict loop
                           gotConflict = true;
                           return;
                        }
                        firstTime = false;
                        ++preCounter;
                        var1.Set (true, at);
                        var2.Get (at);
                        var1.Validate (at);
                        var2.Validate (at);
                        ++postCounter;
                     });
   //with no conflict we should sail straight through (making sure that setting a var in a
   //transaction doesn't make that var invalid in the transaction)
   BOOST_CHECK (!gotConflict);
   BOOST_CHECK_EQUAL (preCounter, 1);
   BOOST_CHECK_EQUAL (postCounter, 1);

   var1.Set (false);
   var2.Set (false);
   
   barrier bar (2);
   std::thread conflicter ([&]()
                           {
                              bar.wait ();
                              var1.Set (true);
                              bar.wait ();
                           });
   preCounter = 0;
   auto middleCounter = 0;
   postCounter = 0;
   firstTime = true;
   WSTM::Atomically ([&](WSTM::WAtomic& at)
                     {
                        ++preCounter;
                        var1.Get (at);
                        var2.Get (at);
                        if (firstTime)
                        {
                           bar.wait ();
                           bar.wait ();
                           firstTime = false;
                        }
                        var2.Validate (at);
                        ++middleCounter;
                        var1.Validate (at);
                        ++postCounter;
                     });
   //this time we had a conflict on var1 so we should have restarted once but var2 should have
   //validated fine
   BOOST_CHECK_EQUAL (preCounter, 2);
   BOOST_CHECK_EQUAL (middleCounter, 2);
   BOOST_CHECK_EQUAL (postCounter, 1);

   conflicter.join ();
}

namespace OnFailedUtils
{
   void NoFailTrans (bool& flag1, WSTM::WVar<bool>& flag2_v, WSTM::WAtomic& at)
   {
      at.OnFail ([&](){flag1 = true;});
      at.OnFail ([&](){flag2_v.Set (true);});
   }
   
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_no_failure)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   WSTM::Atomically ([&](WSTM::WAtomic& at){OnFailedUtils::NoFailTrans (failed1, failed2_v, at);});
   BOOST_CHECK (!failed1);
   BOOST_CHECK (!failed2_v.GetReadOnly ());
}

namespace OnFailedUtils
{
   struct WAborted
   {};

   void AbortTrans (bool& flag1, WSTM::WVar<bool>& flag2_v, WSTM::WAtomic& at)
   {
      at.OnFail ([&]()
                 {
                    flag1 = true;
                 });
      at.OnFail ([&]()
                 {
                    //make sure that transactions work in "on fail" handlers
                    flag2_v.Set (true);
                 });
      throw WAborted ();
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_abort)
{
   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   bool gotExc = false;
   try
   {
      WSTM::Atomically ([&](WSTM::WAtomic& at)
                        {
                           OnFailedUtils::AbortTrans (failed1, failed2_v, at);
                        });
   }
   catch(OnFailedUtils::WAborted&)
   {
      gotExc = true;
   }
   BOOST_REQUIRE (gotExc);
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());
}

namespace OnFailedUtils
{
   void ConflicterThread (const std::shared_ptr<WSTM::WVar<int>>& var_p,
                          const std::shared_ptr<boost::barrier>& bar_p)
   {
      bar_p->wait ();
      var_p->Set (35402);
      bar_p->wait ();
   }

   void ConflictTrans (bool& flag1,
                       WSTM::WVar<bool>& flag2_v,
                       const std::shared_ptr<WSTM::WVar<int>>& var_p,
                       const std::shared_ptr<boost::barrier>& bar_p,
                       WSTM::WAtomic& at)
   {
      at.OnFail ([&](){flag1 = true;});
      at.OnFail ([&](){flag2_v.Set (true);});
      const int val = var_p->Get (at);
      if (val == 0)
      {
         bar_p->wait ();
         bar_p->wait ();      
      }
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_conflict)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   auto var_p = std::make_shared<WSTM::WVar<int>>(0);
   auto bar_p = std::make_shared<boost::barrier> (2);
   std::thread t ([=](){ConflicterThread (var_p, bar_p);});
   WSTM::Atomically (
      [&, bar_p, var_p](WSTM::WAtomic& at)
      {OnFailedUtils::ConflictTrans (failed1, failed2_v, var_p, bar_p, at);});
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());
   t.join ();
}

namespace OnFailedUtils
{
   void RetryUpdateThread (const std::shared_ptr<WSTM::WVar<int>>& var_p,
                           const std::shared_ptr<boost::barrier>& bar_p)
   {
      bar_p->wait ();
      var_p->Set (35402);
   }

   void RetryTrans (bool& flag1,
                    WSTM::WVar<bool>& flag2_v,
                    const std::shared_ptr<WSTM::WVar<int>>& var_p,
                    const std::shared_ptr<boost::barrier>& bar_p,
                    WSTM::WAtomic& at)
   {
      at.OnFail ([&](){flag1 = true;});
      at.OnFail ([&](){flag2_v.Set (true);});
      if (var_p->Get (at) == 0)
      {
         bar_p->wait ();
         WSTM::Retry (at);
      }
   }
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_retry)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   std::shared_ptr<WSTM::WVar<int>> var_p (new WSTM::WVar<int>(0));
   std::shared_ptr<boost::barrier> bar_p (new boost::barrier (2));
   std::thread t ([=](){RetryUpdateThread (var_p, bar_p);});
   WSTM::Atomically (
      [&, bar_p, var_p](WSTM::WAtomic& at)
      {OnFailedUtils::RetryTrans (failed1, failed2_v, var_p, bar_p, at);});
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());
   t.join ();
}

namespace OnFailedUtils
{
   struct NestTrans
   {
      typedef void result_type;
      
      boost::function<void (WSTM::WAtomic&)> m_trans;

      NestTrans (boost::function<void (WSTM::WAtomic&)> trans):
         m_trans (trans)
      {}

      void operator()(WSTM::WAtomic&) const
      {
         WSTM::Atomically (m_trans);
      }
   };
   
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_nested_no_failure)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   WSTM::Atomically (
      NestTrans ([&](WSTM::WAtomic& at) {OnFailedUtils::NoFailTrans (failed1, failed2_v, at);}));
   BOOST_CHECK (!failed1);
   BOOST_CHECK (!failed2_v.GetReadOnly ());
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_nested_abort)
{
   using namespace  OnFailedUtils;
   
   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   bool gotExc = false;
   try
   {
      WSTM::Atomically (
         NestTrans ([&](WSTM::WAtomic& at) {OnFailedUtils::AbortTrans (failed1, failed2_v, at);}));
   }
   catch(WAborted&)
   {
      gotExc = true;
   }
   BOOST_REQUIRE (gotExc);
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_nested_conflict)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   std::shared_ptr<WSTM::WVar<int>> var_p (new WSTM::WVar<int>(0));
   std::shared_ptr<boost::barrier> bar_p (new boost::barrier (2));
   std::thread t ([=](){ConflicterThread (var_p, bar_p);});
   WSTM::Atomically (
      NestTrans (
         [&, var_p, bar_p](WSTM::WAtomic& at)
         {OnFailedUtils::ConflictTrans (failed1, failed2_v, var_p, bar_p, at);}));
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());   
   t.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_OnFailed_nested_retry)
{
   using namespace  OnFailedUtils;

   bool failed1 = false;
   WSTM::WVar<bool> failed2_v (false);
   std::shared_ptr<WSTM::WVar<int>> var_p (new WSTM::WVar<int>(0));
   std::shared_ptr<boost::barrier> bar_p (new boost::barrier (2));
   std::thread t ([=](){RetryUpdateThread (var_p, bar_p);});
   WSTM::Atomically (
      NestTrans (
         [&, var_p, bar_p](WSTM::WAtomic& at)
         {OnFailedUtils::RetryTrans (failed1, failed2_v, var_p, bar_p, at);}));
   BOOST_CHECK (failed1);
   BOOST_CHECK (failed2_v.GetReadOnly ());   
   t.join ();
}

BOOST_AUTO_TEST_CASE (StmVarTests_lambda_atomic)
{
   //checking that Atomically works with c++ lambda objects
   const int INIT_VAL = 846141;
   WSTM::WVar<int> var (INIT_VAL);
   const int INC_VAR = 241;
   const int INC_RES = 5215;
   const int res = WSTM::Atomically ([&](WSTM::WAtomic& at) -> int
                                     {
                                        const int old = var.Get (at);
                                        var.Set (old + INC_VAR, at);
                                        return old + INC_RES;
                                     });
   BOOST_CHECK_EQUAL (res, INIT_VAL + INC_RES);
   BOOST_CHECK_EQUAL (var.GetReadOnly (), INIT_VAL + INC_VAR);
}

BOOST_AUTO_TEST_CASE (StmVarTests_lambda_inconsistent)
{
   //checking that Inconsistently works with c++ lambda objects
   const int INIT_VAL = 846141;
   WSTM::WVar<int> var (INIT_VAL);
   const int res = WSTM::Inconsistently ([&] (WSTM::WInconsistent& inc)
                                         {
                                            return var.GetInconsistent (inc);
                                         });
   BOOST_CHECK_EQUAL (res, INIT_VAL);
}

namespace dtor_trans_during_restart_utils
{
   struct WTransInDtor
   {
      typedef std::shared_ptr<WTransInDtor> Ptr;

      WTransInDtor (WSTM::WVar<int>& v, const int i):
         m_v (v),
         m_i (i)
      {}

      WSTM::WVar<int>& m_v;
      int m_i;
   
      ~WTransInDtor ()
      {
         WSTM::Atomically ([&](WSTM::WAtomic& at)
                           {
                              m_v.Set (m_i, at);
                           });
      }

   private:
      WTransInDtor& operator= (const WTransInDtor&) { return *this; }
   };
}

BOOST_AUTO_TEST_CASE (StmVarTests_dtor_trans_during_restart)
{
   //This tests that running a transaction in the destructor of an
   //object that is being deleted during the clearing of the get/set
   //maps of another transaction works properly. We had a bug where
   //the effects of the transaction run in the destructor would not
   //actually be committed even though the transaction was. This will
   //also cause memory corruption and most likely a crash. See
   //TestTrack issue 9964 for details.

   WSTM::WVar<int> value (0);
   boost::barrier b (2);

   using namespace  dtor_trans_during_restart_utils;
   const int EXPECTED_VALUE = 625065;
   auto transInDtor = WSTM::WVar<WTransInDtor::Ptr>(std::make_shared<WTransInDtor>(value, EXPECTED_VALUE));

   auto Conflictee = [&](WSTM::WAtomic& at)
      {
         //We don't care about the actual value of transInDtor, we
         //just Get it so that it gets stored in our transaction
         transInDtor.Get (at);
         b.wait ();
         //the Conflicter thread will run to completion here changing
         //the value of transInDtor and causing this transaction to be
         //restarted
         b.wait ();
      };

   auto Conflicter = [&](WSTM::WAtomic& at)
      {
         //wait for the Conflictee thread to get the transInDtor value
         b.wait ();
         //this change leaves the only shared_ptr pointing at the
         //original transInDtor value in the Conflictee thread's
         //transaction.
         transInDtor.Set (dtor_trans_during_restart_utils::WTransInDtor::Ptr (), at);
         //We don't want the Conflictee to unblock until after have
         //committed so don't call wait until after the commit finishes 
         at.After ([&](){b.wait ();}); 
      };
   
   std::thread t1 ([=](){WSTM::Atomically (Conflictee);});
   std::thread t2 ([=](){WSTM::Atomically (Conflicter);});
   t2.join ();
   //We need these waits so that Conflictee won't block forever after
   //having its transaction restarted (the first time through the
   //Conflicter thread triggers the barriers)
   b.wait ();
   b.wait ();
   t1.join ();
   const int v = value.GetReadOnly ();
   //If this value is wrong then the transaction that ran in
   //the WTransInDtor destructor didn't commit properly
   BOOST_CHECK_EQUAL (v, EXPECTED_VALUE);   
}

BOOST_AUTO_TEST_SUITE_END(/*StmVarTests*/)

BOOST_AUTO_TEST_SUITE(RunAtomicallyTests)

BOOST_AUTO_TEST_CASE (test_RunAtomically)
{
	struct WTest
	{
		static int Run(WSTM::WVar<int>& v, WSTM::WAtomic& at)
		{
			const int val = v.Get(at);
			v.Set(val + 1, at);
			return val;
		}	
	};

	const int orig = 402455;
	WSTM::WVar<int> v(orig);
					
   auto r = WSTM::RunAtomically (boost::bind(WTest::Run, boost::ref(v), _1));
	const auto res = r();
	BOOST_CHECK_EQUAL(orig, res);
	BOOST_CHECK_EQUAL(orig + 1, v.GetReadOnly());

	const auto res2 = WSTM::RunAtomically(boost::bind(WTest::Run, boost::ref(v), _1))();
	BOOST_CHECK_EQUAL(orig + 1, res2);
	BOOST_CHECK_EQUAL(orig + 2, v.GetReadOnly());
}

BOOST_AUTO_TEST_CASE (test_RunAtomically_void_return)
{
	const int orig = 402455;
	WSTM::WVar<int> v(orig);
   int current = 0;

   const auto Test =
      [&](WSTM::WAtomic& at)
      {
         current = v.Get (at);
         v.Set (current + 1, at);
      };
   auto r = WSTM::RunAtomically (Test);
	r();
	BOOST_CHECK_EQUAL(orig, current);
	BOOST_CHECK_EQUAL(orig + 1, v.GetReadOnly());

	WSTM::RunAtomically(Test)();
	BOOST_CHECK_EQUAL(orig + 1, current);
	BOOST_CHECK_EQUAL(orig + 2, v.GetReadOnly());
}

//TODO: test RunAtomically with options

namespace
{
   struct WMoveOnly
   {
   public:
      int m_value;

      explicit WMoveOnly (const int value) : m_value (value) {}

      WMoveOnly (const WMoveOnly&) = delete;
      void operator= (const WMoveOnly&) = delete;

      WMoveOnly (WMoveOnly&& m) :
         m_value (m.m_value)
      {
         m.m_value = -1;
      }

      WMoveOnly& operator=(WMoveOnly&& m) 
      {
         m_value = m.m_value;
         m.m_value = -1;
         return *this;
      }
   };

}

BOOST_AUTO_TEST_CASE (move_result)
{   
   const auto F = [](const int value, WSTM::WAtomic&) -> WMoveOnly
      {
         return WMoveOnly (value);
      };
   const auto value = 50578;
   const auto m = WSTM::Atomically ([&](WSTM::WAtomic& at){return F (value, at);});
   BOOST_CHECK_EQUAL (m.m_value, value);
}

namespace
{
   struct WCopyOnly
   {
      int m_value;

      explicit WCopyOnly (const int value) : m_value (value) {}
      WCopyOnly (const WCopyOnly& m) :
         m_value (m.m_value)
      {}

      WCopyOnly& operator=(const WCopyOnly& m) 
      {
         m_value = m.m_value;
         return *this;
      }
   };

}

BOOST_AUTO_TEST_CASE (copy_result)
{
   const auto F = [](const int value, WSTM::WAtomic&) -> WCopyOnly
      {
         return WCopyOnly (value);
      };
   const auto value = 50578;
   const auto m = WSTM::Atomically ([&](WSTM::WAtomic& at){return F (value, at);});
   BOOST_CHECK_EQUAL (m.m_value, value);
}

BOOST_AUTO_TEST_SUITE_END(/*RunAtomicallyTests*/)

BOOST_AUTO_TEST_SUITE(BeforeCommitTests)

BOOST_AUTO_TEST_CASE(BeforeCommitTests_test_run)
{
   WSTM::WAtomic* bcAt_p = nullptr;
   auto beforeCommit =
      [&](WSTM::WAtomic& at)
      {
         bcAt_p = &at;
      };
   WSTM::WAtomic* at_p = nullptr;
   auto func =
      [&](WSTM::WAtomic& at)
      {
         at_p = &at;
         at.BeforeCommit (beforeCommit);
      };
   WSTM::Atomically (func);
   BOOST_CHECK (at_p);
   BOOST_CHECK (bcAt_p);
   BOOST_CHECK_EQUAL (bcAt_p, at_p);
}

BOOST_AUTO_TEST_CASE(BeforeCommitTests_test_child_reg)
{
   WSTM::WAtomic* bcAt_p = nullptr;
   auto beforeCommit =
      [&](WSTM::WAtomic& at)
      {
         bcAt_p = &at;
      };
   WSTM::WAtomic* childAt_p = nullptr;
   auto child =
      [&](WSTM::WAtomic& at)
      {
         childAt_p = &at;
         at.BeforeCommit (beforeCommit);
      };      
   WSTM::WAtomic* at_p = nullptr;
   auto func =
      [&](WSTM::WAtomic& at)
      {
         at_p = &at;
         WSTM::Atomically (child);
         BOOST_CHECK (!bcAt_p);
      };
   WSTM::Atomically (func);
   BOOST_CHECK_NE (at_p, childAt_p);
   BOOST_CHECK (at_p);
   BOOST_CHECK (bcAt_p);
   BOOST_CHECK_EQUAL (bcAt_p, at_p);
}

BOOST_AUTO_TEST_CASE(BeforeCommitTests_test_parent_reg)
{
   WSTM::WAtomic* bcAt_p = nullptr;
   auto beforeCommit =
      [&](WSTM::WAtomic& at)
      {
         bcAt_p = &at;
      };
   WSTM::WAtomic* childAt_p = nullptr;
   auto child =
      [&](WSTM::WAtomic& at)
      {
         childAt_p = &at;
      };      
   WSTM::WAtomic* at_p = nullptr;
   auto func =
      [&](WSTM::WAtomic& at)
      {
         at_p = &at;
         at.BeforeCommit (beforeCommit);
         WSTM::Atomically (child);
         BOOST_CHECK (!bcAt_p);
      };
   WSTM::Atomically (func);
   BOOST_CHECK_NE (at_p, childAt_p);
   BOOST_CHECK (at_p);
   BOOST_CHECK (bcAt_p);
   BOOST_CHECK_EQUAL (bcAt_p, at_p);
}

BOOST_AUTO_TEST_SUITE_END(/*BeforeCommitTests*/)

BOOST_AUTO_TEST_SUITE(LocalValueTests)

BOOST_AUTO_TEST_CASE (set_get)
{
   WSTM::WTransactionLocalValue<int> value;
   const auto newValue = 564037;
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         BOOST_CHECK (!value.Get (at));
         BOOST_CHECK_EQUAL (value.Set (newValue, at), newValue);
         BOOST_REQUIRE (value.Get (at));
         BOOST_CHECK_EQUAL (*value.Get (at), newValue);
         //leave a value in the variable at the end to test if it gets carried over to the next transaction
      });

   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         //make sure that the value from the last transaction didn't carry over
         BOOST_CHECK (!value.Get (at));
      });
}

namespace
{
   class MoveOnly
   {
   public:
      int m_value;

      MoveOnly (int value):
         m_value (value)
      {}

      MoveOnly (const MoveOnly&) = delete;
      MoveOnly& operator=(const MoveOnly&) = delete;
      
      MoveOnly (MoveOnly&& m):
         m_value (m.m_value)
      {
         m.m_value = -1;
      }

      MoveOnly& operator=(MoveOnly&& m)
      {
         m_value = m.m_value;
         m.m_value = -1;
         return *this;
      }
   };
}

BOOST_AUTO_TEST_CASE (set_get_move_only)
{
   WSTM::WTransactionLocalValue<MoveOnly> value;
   const auto newValue = 564037;
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         BOOST_CHECK (!value.Get (at));
         auto& setValue = value.Set (MoveOnly (newValue), at);
         BOOST_CHECK_EQUAL (setValue.m_value, newValue);
         BOOST_REQUIRE (value.Get (at));
         BOOST_CHECK_EQUAL (value.Get (at)->m_value, newValue);

         //move the value out of the transaction local variable
         auto m = std::move (*value.Get (at));
         BOOST_CHECK_EQUAL (m.m_value, newValue);

         //make sure we moved from the existing value
         BOOST_REQUIRE (value.Get (at));
         BOOST_CHECK_EQUAL (value.Get (at)->m_value, -1);         
      });
}

BOOST_AUTO_TEST_CASE (no_thread_sharing)
{
   WSTM::WTransactionLocalValue<int> value;
   boost::barrier b (2);
   
   std::thread t1 (WSTM::RunAtomically (
                      [&b, &value](WSTM::WAtomic& at)
                      {
                         const auto newValue = 215177;
                         value.Set (newValue, at);
                         
                         //wait for the other transaction to set it's value
                         b.wait ();
                         
                         //check that we get out value and not the other transactions's
                         BOOST_CHECK (value.Get (at));
                         if (auto val_p = value.Get (at))
                         {
                            BOOST_CHECK_EQUAL (*val_p, newValue);
                         }
                      }));
   
   std::thread t2 (WSTM::RunAtomically (
                      [&b, &value](WSTM::WAtomic& at)
                      {
                         const auto newValue = 301152;
                         value.Set (newValue, at);
                         
                         //wait for the other transaction to set it's value
                         b.wait ();
                         
                         //check that we get out value and not the other transactions's
                         BOOST_CHECK (value.Get (at));
                         if (auto val_p = value.Get (at))
                         {
                            BOOST_CHECK_EQUAL (*val_p, newValue);
                         }
                      }));

   t1.join ();
   t2.join ();
}

BOOST_AUTO_TEST_CASE (multiple_vars)
{
   WSTM::WTransactionLocalValue<int> value1;
   WSTM::WTransactionLocalValue<int> value2;

   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         const auto val1 = 556193;
         value1.Set (val1, at);
         BOOST_REQUIRE (value1.Get (at));

         BOOST_REQUIRE (!value2.Get (at));

         const auto val2 = 322578;
         value2.Set (val2, at);
         BOOST_REQUIRE (value2.Get (at));
         BOOST_CHECK_EQUAL (*value2.Get (at), val2);
         BOOST_REQUIRE (value1.Get (at));
         BOOST_CHECK_EQUAL (*value1.Get (at), val1);
      });
}

namespace
{
   struct WAbortTestTransaction
   {};
}

BOOST_AUTO_TEST_CASE (child_transaction)
{
   WSTM::WTransactionLocalValue<int> value;
   const auto parentVal = 910848;
   const auto childVal = 516048;


   auto Child1 = WSTM::RunAtomically ([&](WSTM::WAtomic& at)
                                      {
                                         BOOST_REQUIRE (value.Get (at));
                                         BOOST_CHECK_EQUAL (*value.Get (at), parentVal);
                                         
                                         value.Set (childVal, at);
                                         BOOST_REQUIRE (value.Get (at));
                                         BOOST_CHECK_EQUAL (*value.Get (at), childVal);

                                         //abort the transaction
                                         throw WAbortTestTransaction ();
                                      });
   auto Child2 = WSTM::RunAtomically ([&](WSTM::WAtomic& at)
                                      {
                                         BOOST_REQUIRE (value.Get (at));
                                         BOOST_CHECK_EQUAL (*value.Get (at), parentVal);

                                         value.Set (childVal, at);
                                         BOOST_REQUIRE (value.Get (at));
                                         BOOST_CHECK_EQUAL (*value.Get (at), childVal);
                                      });
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         value.Set (parentVal, at);

         try
         {
            Child1 ();
            BOOST_FAIL ("Didn't get exception");
         }
         catch(WAbortTestTransaction&)
         {}

         //make sure that the aborted transaction didn't affect the parent
         BOOST_REQUIRE (value.Get (at));
         BOOST_CHECK_EQUAL (*value.Get (at), parentVal);

         Child2 ();
         
         //make sure that the set in the child was merged
         BOOST_REQUIRE (value.Get (at));
         BOOST_CHECK_EQUAL (*value.Get (at), childVal);
      });
}

BOOST_AUTO_TEST_CASE (flag)
{
   WSTM::WTransactionLocalFlag flag;
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         //shoudl be "not set" at start of transaction
         BOOST_CHECK (!flag.TestAndSet (at));
         //now it shoudl be set
         BOOST_CHECK (flag.TestAndSet (at));
         //it shoudl stay set
         BOOST_CHECK (flag.TestAndSet (at));         
      });

   //make sure the value doens't carry over to another transaction
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         //shoudl be "not set" at start of transaction
         BOOST_CHECK (!flag.TestAndSet (at));
         //now it shoudl be set
         BOOST_CHECK (flag.TestAndSet (at));
         //it shoudl stay set
         BOOST_CHECK (flag.TestAndSet (at));         
      });

   //make sure that a value set in the parent is seen by the child
   auto Child1 = WSTM::RunAtomically ([&](WSTM::WAtomic& at)
                                      {
                                         BOOST_CHECK (flag.TestAndSet (at));
                                      });
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         flag.TestAndSet (at);
         Child1 ();
      });

   //make sure that value set in child is seen by the parent
   auto Child2 = WSTM::RunAtomically ([&](WSTM::WAtomic& at)
                                      {
                                         BOOST_CHECK (!flag.TestAndSet (at));
                                      });
   WSTM::Atomically (
      [&](WSTM::WAtomic& at)
      {
         Child2 ();
         BOOST_CHECK (flag.TestAndSet (at));
      });
}

BOOST_AUTO_TEST_SUITE_END(/*LocalValueTests*/)

BOOST_AUTO_TEST_SUITE_END (/*STM*/)
