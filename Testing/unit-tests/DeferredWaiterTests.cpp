/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/DeferredWaiter.h"
using namespace  bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
using boost::bind;
#include <boost/test/unit_test.hpp>
#include <boost/thread.hpp>
#pragma warning (pop)

BOOST_AUTO_TEST_SUITE (DeferredWaiter)

namespace
{
   struct WError
   {};

   template <typename Result_t>
   void WaitFor (const WDeferredResult<Result_t>& waitee, WDeferredValue<Result_t>& deferred)
   {
      if (!waitee.IsDone ())
      {
         deferred.Fail (WError ());
         return;
      }

      if (waitee.Failed ())
      {
         deferred.Fail (WError ());
         return;
      }

      deferred.Done (waitee.GetResult ());
   }
}

BOOST_AUTO_TEST_CASE (test_add)
{
   WDeferredWaiter waiter;
   WDeferredValue<int> src;
   WDeferredResult<int> srcRes (src);
   WDeferredValue<int> dest;
   WDeferredResult<int> destRes (dest);
   waiter.Add (srcRes, bind (WaitFor<int>, _1, dest));
   const int RESULT = 948871;
   src.Done (RESULT);
   BOOST_REQUIRE (destRes.Wait (boost::posix_time::milliseconds (500)));
   BOOST_REQUIRE (!destRes.Failed ());
   BOOST_CHECK_EQUAL (RESULT, destRes.GetResult ());
}

BOOST_AUTO_TEST_CASE (test_cancel)
{
   WDeferredWaiter waiter;
   WDeferredValue<int> src;
   WDeferredResult<int> srcRes (src);
   WDeferredValue<int> dest;
   WDeferredResult<int> destRes (dest);
   const WDeferredWaiter::Tag::Ptr tag_p = waiter.Add (srcRes, bind (WaitFor<int>, _1, dest));
   tag_p->Cancel ();
   const int RESULT = 270844;
   src.Done (RESULT);
   boost::this_thread::sleep (boost::posix_time::milliseconds (50));
   BOOST_CHECK (!destRes.IsDone ());
}

namespace
{
   template <typename Result_t>
   void ThrowError (const WDeferredResult<Result_t>& /*waitee*/)
   {
      throw WError ();
   }
}

BOOST_AUTO_TEST_CASE (test_exceptionInHandler)
{
   //check that a handler throwing an exception doesn't kill the waiter
   WDeferredWaiter waiter;
   WDeferredValue<int> err;
   WDeferredResult<int> errRes (err);
   waiter.Add (errRes, bind (ThrowError<int>, _1));
   err.Done (914026);
   
   WDeferredValue<int> src;
   WDeferredResult<int> srcRes (src);
   WDeferredValue<int> dest;
   WDeferredResult<int> destRes (dest);
   waiter.Add (srcRes, bind (WaitFor<int>, _1, dest));
   const int RESULT = 189465;
   src.Done (RESULT);
   BOOST_REQUIRE (destRes.Wait (boost::posix_time::milliseconds (500)));
   BOOST_REQUIRE (!destRes.Failed ());
   BOOST_CHECK_EQUAL (RESULT, destRes.GetResult ());
}

BOOST_AUTO_TEST_SUITE_END (/*DeferredWaiter*/)
