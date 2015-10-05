/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/RunInThread.h"

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
using boost::bind;
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

using namespace bss::thread;
using namespace bss::thread::STM;

namespace TestResultError
{
	const int SUCCESS_CODE = 94650;
	const int FAILURE_CODE = 12343897;
	
	bool testRun = false;

	void Reset ()
	{
		testRun = false;
	}
	
	struct WException
	{
		WException (int code):
			m_code (code)
		{}

		const int m_code;

   private:
      WException& operator= (const WException&) { return *this; }
	};

	int RunTest (bool error)
	{
		testRun = true;
		if (error)
		{
			throw WRunInThreadError (WException (FAILURE_CODE));
		}
		else
		{
			return SUCCESS_CODE;
		}
	}
}

BOOST_AUTO_TEST_SUITE (RunInThreadTests)

BOOST_AUTO_TEST_CASE (test_result_error)
{
	TestResultError::Reset ();
	WDeferredResult<int> result = RunInThread (bind (TestResultError::RunTest, false));
	BOOST_CHECK_NO_THROW (result.Wait ());
	BOOST_CHECK (result.IsDone ());
	BOOST_CHECK_EQUAL (TestResultError::SUCCESS_CODE, result.GetResult ());
	BOOST_CHECK (TestResultError::testRun);

	TestResultError::Reset ();
	result = RunInThread (bind (TestResultError::RunTest, true));
	result.Wait ();
	bool gotExc = false;
	try
	{
		result.ThrowError ();
	}
	catch (TestResultError::WException& exc)
	{
		gotExc = true;
		BOOST_CHECK_EQUAL (TestResultError::FAILURE_CODE, exc.m_code);
	}
	BOOST_CHECK (gotExc);
	BOOST_CHECK (result.Failed ());
	BOOST_CHECK (TestResultError::testRun);
}

namespace TestNoResultError
{
	const int FAILURE_CODE = 4309687;
	
	bool testRun = false;
	bool gotSuccess = false;

	void Reset ()
	{
		testRun = false;
		gotSuccess = false;
	}

	struct WException
	{
		WException (int m_code_) : m_code (m_code_) {}
		const int m_code;

   private:
      WException& operator= (const WException&) { return *this; }
	};

	void RunTest (bool error)
	{
		testRun = true;
		if (error)
		{
			throw WRunInThreadError (WException (FAILURE_CODE));
		}
		else
		{
			gotSuccess = true;
		}
	}
}

BOOST_AUTO_TEST_CASE (test_no_result_error)
{
	TestNoResultError::Reset ();
	WDeferredResult<void> result = RunInThread (bind (TestNoResultError::RunTest, false));
	BOOST_CHECK_NO_THROW (result.Wait ());
	BOOST_CHECK (result.IsDone ());
	BOOST_CHECK (TestNoResultError::gotSuccess);
	BOOST_CHECK (TestNoResultError::testRun);
	
	TestNoResultError::Reset ();
	result = RunInThread (bind (TestNoResultError::RunTest, true));
	bool gotExc = false;
	try
	{
		result.Wait ();					 
	}
	catch (TestNoResultError::WException& exc)
	{
		gotExc = true;
		BOOST_CHECK_EQUAL (TestNoResultError::FAILURE_CODE, exc.m_code);
	}				
	BOOST_CHECK (result.Failed ());
	BOOST_CHECK (!TestNoResultError::gotSuccess);
	BOOST_CHECK (TestNoResultError::testRun);
}

BOOST_AUTO_TEST_CASE (UnknownException)
{
   struct WUnkownError
   {};
   struct WThrowUnknown
   {
      static int Run ()
      {
         throw WUnkownError ();
      }
   };
	WDeferredResult<int> result = RunInThread (bind (WThrowUnknown::Run));
   result.Wait ();
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (result.Failed ());
   BOOST_CHECK_THROW (result.ThrowError (), WRunInThreadUnknownError);
}

BOOST_AUTO_TEST_SUITE_END (/*RunInThreadTests*/)
