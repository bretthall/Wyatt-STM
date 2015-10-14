/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/Timer.h"
using namespace  bss::thread::STM;
using namespace  boost::chrono;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/test/unit_test.hpp>
#pragma warning (pop)


BOOST_AUTO_TEST_SUITE(STMTimer)

BOOST_AUTO_TEST_CASE (initialization)
{
   const auto before = steady_clock::now ();
   const auto timer = WTimer ();
   const auto after = steady_clock::now ();

   BOOST_CHECK (timer.GetStart () >= before);
   BOOST_CHECK (timer.GetStart () <= after);
}

BOOST_AUTO_TEST_CASE (reset)
{
   auto timer = WTimer ();

   const auto before = steady_clock::now ();
   timer.Restart ();
   const auto after = steady_clock::now ();

   BOOST_CHECK (timer.GetStart () >= before);
   BOOST_CHECK (timer.GetStart () <= after);   
}

BOOST_AUTO_TEST_CASE (elapsed)
{
   const auto timer = WTimer ();

   const auto beforeNS = steady_clock::now ();
   const auto elapsedNS = timer.Elapsed<nanoseconds>();
   const auto afterNS = steady_clock::now ();
   BOOST_CHECK (elapsedNS >= (beforeNS - timer.GetStart ()));
   BOOST_CHECK (elapsedNS <= (afterNS - timer.GetStart ()));

   const auto beforeS = steady_clock::now ();
   const auto elapsedS = timer.Elapsed<seconds>();
   const auto afterS = steady_clock::now ();
   BOOST_CHECK (elapsedS >= duration_cast<seconds>(beforeS - timer.GetStart ()));
   BOOST_CHECK (elapsedS <= duration_cast<seconds>(afterS - timer.GetStart ()));

   const auto beforeSF = steady_clock::now ();
   const auto elapsedSF = timer.ElapsedSeconds();
   const auto afterSF = steady_clock::now ();
   BOOST_CHECK (elapsedSF >= duration<double>(beforeSF - timer.GetStart ()).count ());
   BOOST_CHECK (elapsedSF <= duration<double>(afterSF - timer.GetStart ()).count ());
}

BOOST_AUTO_TEST_SUITE_END(/*STMTimer*/)
