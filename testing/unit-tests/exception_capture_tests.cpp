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

#include "exception_capture.h"
using namespace WSTM;

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_SUITE (ExceptionCapture)

BOOST_AUTO_TEST_CASE (test_throw_empty)
{
	WExceptionCapture empty;
	BOOST_CHECK_NO_THROW(empty.ThrowCaptured());
}

BOOST_AUTO_TEST_CASE (test_capture_by_ctor)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
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
   WExceptionCapture exc;
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

	WExceptionCapture empty;
	BOOST_CHECK_NO_THROW(empty.ThrowCaptured());
}

BOOST_AUTO_TEST_CASE (test_copy)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
	WExceptionCapture exc2(exc);
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

BOOST_AUTO_TEST_CASE (test_assign)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
	WExceptionCapture exc2;
   exc2 = exc;
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

BOOST_AUTO_TEST_CASE (test_move)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
	WExceptionCapture exc2(std::move (exc));
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

BOOST_AUTO_TEST_CASE (test_move_assign)
{
	const std::string testMsg = "testing 1 2 3";
   WTestExc captured (testMsg);
   WExceptionCapture exc (captured);
	WExceptionCapture exc2;
   exc2 = std::move (exc);
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

BOOST_AUTO_TEST_CASE (test_operator_bool)
{
	WExceptionCapture w;
	BOOST_CHECK(!w);
	w.Capture(WTestExc("123"));
	BOOST_CHECK(w);
}

BOOST_AUTO_TEST_CASE (test_has_captured)
{
	WExceptionCapture w;
	BOOST_CHECK(!Atomically ([&](WAtomic& at){return w.HasCaptured (at);}));
	w.Capture(WTestExc("123"));
   BOOST_CHECK(Atomically ([&](WAtomic& at){return w.HasCaptured (at);}));
}

BOOST_AUTO_TEST_CASE (capture_another_capture)
{
   WExceptionCapture w1;
   WExceptionCapture w2;
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
   WExceptionCapture w2;
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
   WExceptionCapture w (captured);
   w.Reset ();
   BOOST_CHECK (!w);

   //make sure that resetting an empty capture is OK
   BOOST_CHECK_NO_THROW (w.Reset ());
}

BOOST_AUTO_TEST_CASE (CaptureInsideVar)
{
   //boost::thread_specific_ptr can have issues when crossing the
   //boudnary from main program code into DLL code. Storing a
   //WExceptionCapture inside of an object that is itself stored
   //in a WVar has caused this problem to surface in the past. If the
   //problem is occuring you will get an assertion (not boost test
   //assertion) when the transaction within which the exception is
   //captured commits.
   struct WTestObject
   {
      WExceptionCapture m_cap;
   };
   WVar<std::shared_ptr<WTestObject> > obj_v (
      std::shared_ptr<WTestObject>(new WTestObject));

   struct WTestError
   {
      WTestError (const int val) :
         m_val (val)
      {}
      int m_val;
   };
   static const int VALUE = 979222;
   Atomically ([&](WAtomic& at)
               {
                  std::shared_ptr<WTestObject> obj_p = obj_v.Get (at);
                  obj_p->m_cap.Capture (WTestError (VALUE), at);
               });
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
