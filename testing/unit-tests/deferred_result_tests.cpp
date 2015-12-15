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

#include "deferred_result.h"
using namespace  WSTM;

#include <boost/test/unit_test.hpp>

namespace
{
	struct WTestException
	{
		WTestException (int code): m_code (code) {}
		const int m_code;

   private:
      WTestException& operator= (const WTestException&) { return *this; }
	};	

   auto CountCallback (int& count)
   {
      return [&count](){++count;};
   }

}

BOOST_AUTO_TEST_SUITE (DeferredResult)

BOOST_AUTO_TEST_CASE (InvalidResult)
{
   const auto DoNothing = [](){};
   
   WDeferredResult<int> result;
   BOOST_CHECK (!result);
   BOOST_CHECK_NO_THROW (result.Release ());
   BOOST_CHECK_THROW (result.IsDone (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.Failed (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.Wait (std::chrono::milliseconds (0)), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.GetResult (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.ThrowError (), WInvalidDeferredResultError);
   BOOST_CHECK_THROW (result.OnDone (DoNothing), WInvalidDeferredResultError);

   Atomically ([&](WAtomic& at)
               {
                  BOOST_CHECK_NO_THROW (result.Release ());
                  BOOST_CHECK_THROW (result.IsDone (at), WInvalidDeferredResultError);
                  BOOST_CHECK_THROW (result.Failed (at), WInvalidDeferredResultError);
                  BOOST_CHECK_THROW (result.RetryIfNotDone (at, std::chrono::milliseconds (0)), WInvalidDeferredResultError);
                  BOOST_CHECK_THROW (result.GetResult (at), WInvalidDeferredResultError);
                  BOOST_CHECK_THROW (result.ThrowError (at), WInvalidDeferredResultError);
                  BOOST_CHECK_THROW (result.OnDone (DoNothing, at), WInvalidDeferredResultError);
               });
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
   Atomically ([&](WAtomic& at)
               {
                  BOOST_CHECK (!result.IsDone (at));
                  BOOST_CHECK (!value.IsDone (at));
                  BOOST_CHECK_THROW (result.Failed (at), WNotDoneError);
                  BOOST_CHECK_THROW (result.GetResult (at), WNotDoneError);
                  BOOST_CHECK_THROW (result.ThrowError (at), WNotDoneError);
               });
   
   int count = 0;
   BOOST_CHECK_NO_THROW (result.OnDone (CountCallback (count)));
   BOOST_CHECK_EQUAL (0, count);
   count = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (count), at);});
   BOOST_CHECK_EQUAL (0, count);
   
   BOOST_CHECK (!result.Wait (std::chrono::milliseconds (1)));
   BOOST_CHECK_THROW (Atomically ([&](WAtomic& at){result.RetryIfNotDone (at, std::chrono::milliseconds (1));}), WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (IntFail)
{
   static const int FAIL_VALUE = 271293;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (preCount2), at);});

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

   Atomically ([&](WAtomic& at)
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
               });

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (postCount2), at);});
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (std::chrono::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically ([&](WAtomic& at){result.RetryIfNotDone (at, std::chrono::milliseconds (1));}));
}

BOOST_AUTO_TEST_CASE (IntFailAtomic)
{
   static const int FAIL_VALUE = 312748;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (preCount2), at);});

   Atomically ([&](WAtomic& at){value.Fail (WTestException (FAIL_VALUE), at);});
   
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

   Atomically ([&](WAtomic& at)
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
               });
   
   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (postCount2), at);});
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (std::chrono::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically ([&](WAtomic& at){result.RetryIfNotDone (at, std::chrono::milliseconds (1));}));
}

BOOST_AUTO_TEST_CASE (IntSuccess)
{
   static const int VALUE = 239352;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (preCount2), at);});

   value.Done (VALUE);
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (!result.Failed ());
   BOOST_CHECK_EQUAL (result.GetResult (), VALUE);
   BOOST_CHECK_NO_THROW (result.ThrowError ());

   Atomically ([&](WAtomic& at)
               {
                  BOOST_CHECK (value.IsDone (at));
                  BOOST_CHECK (result.IsDone (at));
                  BOOST_CHECK (!result.Failed (at));
                  BOOST_CHECK_EQUAL (result.GetResult (at), VALUE);
                  BOOST_CHECK_NO_THROW (result.ThrowError (at));
               });

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (postCount2), at);});
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (std::chrono::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically ([&](WAtomic& at){result.RetryIfNotDone (at, std::chrono::milliseconds (1));}));
}

BOOST_AUTO_TEST_CASE (IntSuccessAtomic)
{
   static const int VALUE = 125627;
   WDeferredValue<int> value;
   WDeferredResult<int> result (value);

   int preCount1 = 0;
   result.OnDone (CountCallback (preCount1));
   int preCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (preCount2), at);});

   Atomically ([&](WAtomic& at){value.Done (VALUE, at);});
   
   BOOST_CHECK (value.IsDone ());
   BOOST_CHECK (result.IsDone ());
   BOOST_CHECK (!result.Failed ());
   BOOST_CHECK_EQUAL (result.GetResult (), VALUE);
   BOOST_CHECK_NO_THROW (result.ThrowError ());

   Atomically ([&](WAtomic& at)
               {
                  BOOST_CHECK (value.IsDone (at));
                  BOOST_CHECK (result.IsDone (at));
                  BOOST_CHECK (!result.Failed (at));
                  BOOST_CHECK_EQUAL (result.GetResult (at), VALUE);
                  BOOST_CHECK_NO_THROW (result.ThrowError (at));
               });

   BOOST_CHECK_EQUAL (1, preCount1);
   BOOST_CHECK_EQUAL (1, preCount2);
   int postCount1 = 0;
   result.OnDone (CountCallback (postCount1));
   BOOST_CHECK_EQUAL (1, postCount1);
   int postCount2 = 0;
   Atomically ([&](WAtomic& at){result.OnDone (CountCallback (postCount2), at);});
   BOOST_CHECK_EQUAL (1, postCount2);
   BOOST_CHECK (result.Wait (std::chrono::milliseconds (1)));
   BOOST_CHECK_NO_THROW (Atomically ([&](WAtomic& at){result.RetryIfNotDone (at, std::chrono::milliseconds (1));}));
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
   {
      WDeferredValue<int> value;
      result = value;
   }
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
