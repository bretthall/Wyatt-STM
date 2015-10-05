/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/ExceptionCaptureAtomic.h"
#include "BSS/Thread/STM/stm.h"
using bss::WExceptionCapture;
using namespace bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

namespace
{
	struct WTestExc
	{
		WTestExc (const std::string& msg):
			m_msg (msg)
		{}

		const std::string m_msg;

   private:
      WTestExc& operator= (const WTestExc&) { return *this; }
	};
}

BOOST_AUTO_TEST_SUITE (ExceptionCaptureAtomic)

BOOST_AUTO_TEST_CASE (test_throw_empty)
{
	WExceptionCaptureAtomic empty;
	BOOST_CHECK_NO_THROW(empty.ThrowCaptured());
}

BOOST_AUTO_TEST_CASE (test_capture_by_ctor)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCaptureAtomic exc (captured);
	bool gotExc = false;
	std::string excMsg;
	try
	{
		exc.ThrowCaptured();
	}
	catch(WTestExc& testExc)
	{
		gotExc = true;
		excMsg = testExc.m_msg;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(testMsg, excMsg);
}

BOOST_AUTO_TEST_CASE (test_captured_by_method)
{
	const std::string testMsg = "testing 1 2 3";
   WExceptionCaptureAtomic exc;
   exc.Capture(WTestExc(testMsg));
	bool gotExc = false;
	std::string excMsg;
	try
	{
		exc.ThrowCaptured();
	}
	catch(WTestExc& testExc)
	{
		gotExc = true;
		excMsg = testExc.m_msg;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(testMsg, excMsg);

	WExceptionCaptureAtomic empty;
	BOOST_CHECK_NO_THROW(empty.ThrowCaptured());
}

BOOST_AUTO_TEST_CASE (test_copy)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCaptureAtomic exc (captured);
	WExceptionCaptureAtomic exc2(exc);
   bool gotExc = false;
	std::string excMsg;
	try
	{
		exc2.ThrowCaptured();
	}
	catch(WTestExc& testExc)
	{
		gotExc = true;
		excMsg = testExc.m_msg;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(testMsg, excMsg);    
}

BOOST_AUTO_TEST_CASE (test_copy_non_atomic_class)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
	WExceptionCaptureAtomic exc2(exc);
   bool gotExc = false;
	std::string excMsg;
	try
	{
		exc2.ThrowCaptured();
	}
	catch(WTestExc& testExc)
	{
		gotExc = true;
		excMsg = testExc.m_msg;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(testMsg, excMsg);    
}

BOOST_AUTO_TEST_CASE (test_capture_atomic)
{
	const std::string testMsg = "testing 1 2 3";
	WExceptionCaptureAtomic exc;
   struct Capture
   {
      static void Run (WExceptionCaptureAtomic& exc,
                       const std::string& testMsg,
                       WAtomic& at)
      {
         exc.Capture (WTestExc (testMsg), at);
      }
   };

   Atomically(boost::bind(Capture::Run, boost::ref(exc), testMsg, _1));
	bool gotExc = false;
	std::string excMsg;
   struct WAtomicThrow
   {
      static void Run (WExceptionCaptureAtomic& exc, WAtomic& at)
      {
         exc.ThrowCaptured (at);
      }
   };
	try
	{
		Atomically(boost::bind(WAtomicThrow::Run, boost::ref(exc), _1));
	}
	catch(WTestExc& testExc)
	{
		gotExc = true;
		excMsg = testExc.m_msg;
	}
	BOOST_CHECK(gotExc);
	BOOST_CHECK_EQUAL(testMsg, excMsg);

	WExceptionCaptureAtomic empty;
	BOOST_CHECK_NO_THROW(Atomically(boost::bind (WAtomicThrow::Run, boost::ref(empty), _1)));
}

BOOST_AUTO_TEST_CASE (test_operator_bool)
{
	WExceptionCaptureAtomic w;
	BOOST_CHECK(!w);
	w.Capture(WTestExc("123"));
	BOOST_CHECK(w);
}

BOOST_AUTO_TEST_CASE (test_has_captured)
{
	WExceptionCaptureAtomic w;
	BOOST_CHECK(
		!Atomically(boost::bind(&WExceptionCaptureAtomic::HasCaptured, boost::ref(w), _1)));
	w.Capture(WTestExc("123"));
	BOOST_CHECK(
		Atomically(boost::bind(&WExceptionCaptureAtomic::HasCaptured, boost::ref(w), _1)));
}

BOOST_AUTO_TEST_CASE (capture_another_capture)
{
   WExceptionCaptureAtomic w1;
   WExceptionCaptureAtomic w2;
   const std::string val = "612748";
   w1.Capture (WTestExc (val));
   w2.Capture (w1);
	bool gotExc = false;
	try
	{
		w2.ThrowCaptured();
	}
	catch(WTestExc& exc)
	{
		BOOST_CHECK_EQUAL(val, exc.m_msg);
		gotExc = true;
	}
	BOOST_CHECK(gotExc);   
}


BOOST_AUTO_TEST_CASE (capture_another_capture_non_atomic)
{
   WExceptionCapture w1;
   WExceptionCaptureAtomic w2;
   const std::string val = "612748";
   w1.Capture (WTestExc (val));
   w2.Capture (w1);
	bool gotExc = false;
	try
	{
		w2.ThrowCaptured();
	}
	catch(WTestExc& exc)
	{
		BOOST_CHECK_EQUAL(val, exc.m_msg);
		gotExc = true;
	}
	BOOST_CHECK(gotExc);   
}

BOOST_AUTO_TEST_CASE (reset)
{
   WTestExc captured ("959106");
   WExceptionCaptureAtomic w (captured);
   w.Reset ();
   BOOST_CHECK (!w);

   //make sure that resetting an empty capture is OK
   BOOST_CHECK_NO_THROW (w.Reset ());
}

BOOST_AUTO_TEST_CASE (CaptureInsideVar)
{
   //boost::thread_specific_ptr can have issues when crossing the
   //boudnary from main program code into DLL code. Storing a
   //WExceptionCaptureAtomic inside of an object that is itself stored
   //in a WVar has caused this problem to surface in the past. If the
   //problem is occuring you will get an assertion (not boost test
   //assertion) when the transaction within which the exception is
   //captured commits.
   struct WTestObject
   {
      WExceptionCaptureAtomic m_cap;
   };
   WVar<boost::shared_ptr<WTestObject> > obj_v (
      boost::shared_ptr<WTestObject>(new WTestObject));

   struct WTestError
   {
      WTestError (const int val) :
         m_val (val)
      {}
      int m_val;
   };
   static const int VALUE = 979222;
   struct CaptureError
   {
      static void Run (WVar<boost::shared_ptr<WTestObject> >& obj_v, WAtomic& at)
      {
         boost::shared_ptr<WTestObject> obj_p = obj_v.Get (at);
         obj_p->m_cap.Capture (WTestError (VALUE), at);
      }
   };
   Atomically (boost::bind (CaptureError::Run, boost::ref (obj_v), _1));

   bool gotExc = false;
   try
   {
      obj_v.GetReadOnly ()->m_cap.ThrowCaptured ();
   }
   catch (WTestError& exc)
   {
      gotExc = true;
      BOOST_CHECK_EQUAL (VALUE, exc.m_val);
   }
   BOOST_CHECK (gotExc);
}

BOOST_AUTO_TEST_SUITE_END (/*ExceptionCaptureAtomic*/)
