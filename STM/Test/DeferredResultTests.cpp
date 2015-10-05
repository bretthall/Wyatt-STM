/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/DeferredResult.h"
#include "BSS/Thread/STM/stm.h"

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
using boost::bind;
using boost::ref;
using boost::cref;
#include <boost/shared_ptr.hpp>
using boost::shared_ptr;
#include <boost/thread/thread.hpp>
using boost::thread;
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

using namespace bss::thread;
using namespace bss::thread::STM;
namespace bpt = boost::posix_time;

namespace
{
	struct WTestException
	{
		WTestException (int code): m_code (code) {}
		const int m_code;

   private:
      WTestException& operator= (const WTestException&) { return *this; }
	};	

	struct WTestThread
	{
		WTestThread (unsigned int sleep, double result, int failCode, bool fail):
			m_sleep (sleep), m_resultVal (result), m_failCode (failCode), m_fail (fail)
		{}

		void Start ()
		{
			m_thread.reset (new thread (ref (*this)));
		}
		
		void operator () ()
		{
			boost::this_thread::sleep (boost::posix_time::milliseconds (m_sleep));
			if (m_fail)
			{
				m_result.Fail (WTestException (m_failCode));
			}
			else
			{
				m_result.Done (m_resultVal);
			}
		}
		
		WDeferredValue<double> m_result;
		unsigned int m_sleep;
		double m_resultVal;
		int m_failCode;
		bool m_fail;

		shared_ptr<thread> m_thread;
	};

   struct CountCallback
   {
      CountCallback (int& count):
         m_count (count)
      {}

      void operator()()
      {
         ++m_count;
      }

      int& m_count;

   private:
      CountCallback& operator= (const CountCallback&) { return *this; }
   };

}

BOOST_AUTO_TEST_SUITE (DeferredResult)

BOOST_AUTO_TEST_CASE (InvalidResult)
{
   struct Callback
   {
      static void run () {}
   };

   WDeferredResult<int> result;
   BOOST_CHECK (!result);
   BOOST_CHECK_NO_THROW (result.Release ());
   BOOST_CHECK_THROW (result.IsDone (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.Failed (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.Wait (bpt::milliseconds (0)), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.GetResult (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.ThrowError (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.OnDone (Callback::run), WInvalidDeferredResultError);
   
   struct WAtomicTests
   {
      static void Run (WDeferredResult<int>& result, WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (result.Release ());
         BOOST_CHECK_THROW (result.IsDone (at), WInvalidDeferredResultError);
         BOOST_CHECK_THROW (result.Failed (at), WInvalidDeferredResultError);
         BOOST_CHECK_THROW (result.RetryIfNotDone (at, bpt::milliseconds (0)),
                            WInvalidDeferredResultError);
         BOOST_CHECK_THROW (result.GetResult (at), WInvalidDeferredResultError);
         BOOST_CHECK_THROW (result.ThrowError (at), WInvalidDeferredResultError);
         BOOST_CHECK_THROW (result.OnDone (Callback::run, at), WInvalidDeferredResultError);
      }
   };
   Atomically (bind (WAtomicTests::Run, ref (result), _1));
}

BOOST_AUTO_TEST_CASE (initialization_from_value)
{
   WDeferredValue<int> value;
   WDeferredResult<int> result1 (value);
   BOOST_CHECK (result1);

   Atomically ([&](WAtomic& at)
               {
                  WDeferredResult<int> result (value, at);
                  BOOST_CHECK (result);
               });

   WDeferredResult<int> result2;
   result2 = value;
   BOOST_CHECK (result2);

   WDeferredResult<int> result3;
   Atomically ([&](WAtomic& at){result3.Init (value, at);});
   BOOST_CHECK (result3);
}

BOOST_AUTO_TEST_CASE (initialization_from_other_result)
{
   WDeferredValue<int> value;
   WDeferredResult<int> origResult (value);
   BOOST_REQUIRE (origResult);

   WDeferredResult<int> result1 (origResult);
   BOOST_CHECK (result1);
   
   Atomically ([&](WAtomic& at)
               {
                  WDeferredResult<int> result (origResult, at);
                  BOOST_CHECK (result);
               });

   WDeferredResult<int> result2;
   result2 = origResult;
   BOOST_CHECK (result2);

   WDeferredResult<int> result3;
   Atomically ([&](WAtomic& at){result3.Copy (origResult, at);});
   BOOST_CHECK (result3);
}

BOOST_AUTO_TEST_CASE (NotDone)
{
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);
   BOOST_CHECK (!result.IsDone ());
   BOOST_CHECK (!value.IsDone ());
   BOOST_CHECK_THROW (result.Failed (), WNotDoneError);
   BOOST_CHECK_THROW (result.GetResult (), WNotDoneError);
   BOOST_CHECK_THROW (result.ThrowError (), WNotDoneError);
   struct WAtomicTests
   {
      static void Run (WDeferredValue<int>& value, WDeferredResult<int>& result, WAtomic& at)
      {
         BOOST_CHECK (!result.IsDone (at));
         BOOST_CHECK (!value.IsDone (at));
         BOOST_CHECK_THROW (result.Failed (at), WNotDoneError);
         BOOST_CHECK_THROW (result.GetResult (at), WNotDoneError);
         BOOST_CHECK_THROW (result.ThrowError (at), WNotDoneError);
      }
   };
   Atomically (bind (WAtomicTests::Run, ref (value), ref (result), _1));
   
   int count = 0;
   BOOST_CHECK_NO_THROW (result.OnDone (CountCallback (count)));
   BOOST_CHECK_EQUAL (0, count);
   count = 0;
   struct WAtomicOnDone
   {
      static void Run (WDeferredResult<int>& result, int& count, WAtomic& at)
      {
         result.OnDone (CountCallback (count), at);
      }
   };
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (count), _1));
   BOOST_CHECK_EQUAL (0, count);
   
   BOOST_CHECK (!result.Wait (bpt::milliseconds (1)));
   BOOST_CHECK_THROW (Atomically (bind (&WDeferredResult<int>::RetryIfNotDone,
                                        result, _1, bpt::milliseconds (1))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (IntFail)
{
   static const int FAIL_VALUE = 271293;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   struct WAtomicOnDone
   {
      static void Run (WDeferredResult<int>& result, int& count, WAtomic& at)
      {
         result.OnDone (CountCallback (count), at);
      }
   };
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (preCount2), _1));

   value.Fail (WTestException (FAIL_VALUE));
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (result.Failed ());
   BOOST_CHECK_THROW (result.GetResult (), WTestException);
   BOOST_CHECK_THROW (result.ThrowError (), WTestException);
   try
   {
      result.ThrowError ();
   }
   catch (WTestException& exc)
   {
      BOOST_CHECK_EQUAL (FAIL_VALUE, exc.m_code);
   }

   struct WAtomicTests
   {
      static void Run (WDeferredValue<int>& value,
                       WDeferredResult<int>& result,
                       int& count,
                       WAtomic& at)
      {
         (void)count;
         BOOST_CHECK (value.IsDone (at));
         BOOST_CHECK (result.IsDone (at));
         BOOST_CHECK (result.Failed (at));
         BOOST_CHECK_THROW (result.GetResult (at), WTestException);
         BOOST_CHECK_THROW (result.ThrowError (at), WTestException);
         try
         {
            result.ThrowError (at);
         }
         catch (WTestException& exc)
         {
            BOOST_CHECK_EQUAL (FAIL_VALUE, exc.m_code);
         }         
      } 
   };

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (postCount2), _1));
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (bpt::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically (bind (&WDeferredResult<int>::RetryIfNotDone,
                                           result, _1, bpt::milliseconds (1))));
}

BOOST_AUTO_TEST_CASE (IntFailAtomic)
{
   static const int FAIL_VALUE = 312748;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   struct WAtomicOnDone
   {
      static void Run (WDeferredResult<int>& result, int& count, WAtomic& at)
      {
         result.OnDone (CountCallback (count), at);
      }
   };
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (preCount2), _1));

   struct WAtomicFail
   {
      static void Run (WDeferredValue<int>& value, WAtomic& at)
      {
         value.Fail (WTestException (FAIL_VALUE), at);
      }
   };
   Atomically (bind (WAtomicFail::Run, ref (value), _1));
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (result.Failed ());
   BOOST_CHECK_THROW (result.GetResult (), WTestException);
   BOOST_CHECK_THROW (result.ThrowError (), WTestException);
   try
   {
      result.ThrowError ();
   }
   catch (WTestException& exc)
   {
      BOOST_CHECK_EQUAL (FAIL_VALUE, exc.m_code);
   }

   struct WAtomicTests
   {
      static void Run (WDeferredValue<int>& value,
                       WDeferredResult<int>& result,
                       WAtomic& at)
      {
         BOOST_CHECK (value.IsDone (at));
         BOOST_CHECK (result.IsDone (at));
         BOOST_CHECK (result.Failed (at));
         BOOST_CHECK_THROW (result.GetResult (at), WTestException);
         BOOST_CHECK_THROW (result.ThrowError (at), WTestException);
         try
         {
            result.ThrowError (at);
         }
         catch (WTestException& exc)
         {
            BOOST_CHECK_EQUAL (FAIL_VALUE, exc.m_code);
         }         
      } 
   };
   Atomically (bind (WAtomicTests::Run, ref (value), ref (result), _1));
   
   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (postCount2), _1));
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (bpt::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically (bind (&WDeferredResult<int>::RetryIfNotDone,
                                           result, _1, bpt::milliseconds (1))));
}

BOOST_AUTO_TEST_CASE (IntSuccess)
{
   static const int VALUE = 239352;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   struct WAtomicOnDone
   {
      static void Run (WDeferredResult<int>& result, int& count, WAtomic& at)
      {
         result.OnDone (CountCallback (count), at);
      }
   };
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (preCount2), _1));

   value.Done (VALUE);
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (!result.Failed ());
   BOOST_CHECK_EQUAL (result.GetResult (), VALUE);
   BOOST_CHECK_NO_THROW (result.ThrowError ());

   struct WAtomicTests
   {
      static void Run (WDeferredValue<int>& value,
                       WDeferredResult<int>& result,
                       WAtomic& at)
      {
         BOOST_CHECK (value.IsDone (at));
         BOOST_CHECK (result.IsDone (at));
         BOOST_CHECK (!result.Failed (at));
         BOOST_CHECK_EQUAL (result.GetResult (at), VALUE);
         BOOST_CHECK_NO_THROW (result.ThrowError (at));
      } 
   };
   Atomically (bind (WAtomicTests::Run, ref (value), ref (result), _1));

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (postCount2), _1));
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (bpt::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically (bind (&WDeferredResult<int>::RetryIfNotDone,
                                           result, _1, bpt::milliseconds (1))));
}

BOOST_AUTO_TEST_CASE (IntSuccessAtomic)
{
   static const int VALUE = 125627;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   struct WAtomicOnDone
   {
      static void Run (WDeferredResult<int>& result, int& count, WAtomic& at)
      {
         result.OnDone (CountCallback (count), at);
      }
   };
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (preCount2), _1));

   struct WAtomicDone
   {
      static void Run (WDeferredValue<int>& value, WAtomic& at)
      {
         value.Done (VALUE, at);
      }
   };
   Atomically (bind (WAtomicDone::Run, ref (value), _1));
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (!result.Failed ());
   BOOST_CHECK_EQUAL (result.GetResult (), VALUE);
   BOOST_CHECK_NO_THROW (result.ThrowError ());

   struct WAtomicTests
   {
      static void Run (WDeferredValue<int>& value,
                       WDeferredResult<int>& result,
                       WAtomic& at)
      {
         BOOST_CHECK (value.IsDone (at));
         BOOST_CHECK (result.IsDone (at));
         BOOST_CHECK (!result.Failed (at));
         BOOST_CHECK_EQUAL (result.GetResult (at), VALUE);
         BOOST_CHECK_NO_THROW (result.ThrowError (at));
      } 
   };
   Atomically (bind (WAtomicTests::Run, ref (value), ref (result), _1));

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically (bind (WAtomicOnDone::Run, ref (result), ref (postCount2), _1));
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (bpt::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically (bind (&WDeferredResult<int>::RetryIfNotDone,
                                           result, _1, bpt::milliseconds (1))));
}

BOOST_AUTO_TEST_CASE (CopyValue)
{
   WDeferredValue<int> original;
   WDeferredValue<int> copy1 (original);
   WDeferredValue<int> copy2;
   copy2 = original;
   
   const int VALUE = 481049;
   original.Done (VALUE);
   
   WDeferredResult<int> result1 (copy1);
   BOOST_CHECK (copy1.IsDone ());
   BOOST_CHECK (result1.IsDone ());
   BOOST_CHECK_EQUAL (result1.GetResult (), VALUE);
   WDeferredResult<int> result2 (copy2);
   BOOST_CHECK (copy2.IsDone ());
   BOOST_CHECK (result2.IsDone ());
   BOOST_CHECK_EQUAL (result2.GetResult (), VALUE);
}

BOOST_AUTO_TEST_CASE (CopyResult)
{
   WDeferredValue<int> value;
   const int VALUE = 81294;
   value.Done (VALUE);
   WDeferredResult<int> original (value);
   WDeferredResult<int> copy1 (original);
   WDeferredResult<int> copy2;
   copy2 = original;
   original.Release ();
   BOOST_CHECK (copy1);
   BOOST_CHECK (copy1.IsDone ());
   BOOST_CHECK_EQUAL (copy1.GetResult (), VALUE);
   BOOST_CHECK (copy2);
   BOOST_CHECK (copy2.IsDone ());
   BOOST_CHECK_EQUAL (copy2.GetResult (), VALUE);
}

BOOST_AUTO_TEST_CASE (BrokenPromise)
{
   WDeferredResult<int> result;
#pragma warning (push)
#pragma warning (disable : 4127)
   if (true)
   {
      WDeferredValue<int> value;
      result = value;
   }
#pragma warning (pop)
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK_THROW (result.ThrowError (), WBrokenPromiseError);
}

BOOST_AUTO_TEST_CASE (has_readers)
{
   WDeferredValue<int> value;
   BOOST_CHECK (!value.HasReaders ());

   WDeferredResult<int> result1 (value);
   BOOST_CHECK (value.HasReaders ());
   result1.Release ();
   BOOST_CHECK (!value.HasReaders ());

   WDeferredResult<int> result2;
   result2 = value;
   BOOST_CHECK (value.HasReaders ());
   result2.Release ();
   BOOST_CHECK (!value.HasReaders ());

   result1 = value;
   BOOST_CHECK (value.HasReaders ());
   result2 = result1;
   BOOST_CHECK (value.HasReaders ());
   result1 = WDeferredResult<int>();
   BOOST_CHECK (value.HasReaders ());
   result2 = WDeferredResult<int>();
   BOOST_CHECK (!value.HasReaders ());
}

BOOST_AUTO_TEST_SUITE_END (/*DeferredResult*/)
