/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2014. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "../LifetimeMonitor.h"
using namespace  bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

BOOST_AUTO_TEST_SUITE(LifetimeMonitorTests)

BOOST_AUTO_TEST_CASE (default_ctor)
{
   auto monitor = WLifetimeMonitor ();
   BOOST_CHECK (!monitor.IsAlive ());
}

BOOST_AUTO_TEST_CASE (beacon_ctor)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor = WLifetimeMonitor (*beacon_p);
   BOOST_CHECK (monitor.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor.IsAlive ());
}

BOOST_AUTO_TEST_CASE (monitor)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor = WLifetimeMonitor ();
   monitor.Monitor (*beacon_p);
   BOOST_CHECK (monitor.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor.IsAlive ());
}

BOOST_AUTO_TEST_CASE (monitor_copy_ctor)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor1 = WLifetimeMonitor (*beacon_p);
   auto monitor2 = WLifetimeMonitor (monitor1);
   BOOST_CHECK (monitor1.IsAlive ());
   BOOST_CHECK (monitor2.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor1.IsAlive ());
   BOOST_CHECK (!monitor2.IsAlive ());
}

BOOST_AUTO_TEST_CASE (monitor_assignment)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor1 = WLifetimeMonitor (*beacon_p);
   auto monitor2 = WLifetimeMonitor ();
   monitor2 = monitor1;
   BOOST_CHECK (monitor1.IsAlive ());
   BOOST_CHECK (monitor2.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor1.IsAlive ());
   BOOST_CHECK (!monitor2.IsAlive ());
}

BOOST_AUTO_TEST_CASE (monitor_move_ctor)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor1 = WLifetimeMonitor (*beacon_p);
   auto monitor2 = WLifetimeMonitor (std::move (monitor1));
   BOOST_CHECK (monitor2.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor2.IsAlive ());
}

BOOST_AUTO_TEST_CASE (monitor_move_operator)
{
   auto beacon_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto monitor1 = WLifetimeMonitor (*beacon_p);
   auto monitor2 = WLifetimeMonitor ();
   monitor2 = std::move (monitor1);
   BOOST_CHECK (monitor2.IsAlive ());
   beacon_p.reset ();
   BOOST_CHECK (!monitor2.IsAlive ());
}

BOOST_AUTO_TEST_CASE (beacon_move_ctor)
{
   WLifetimeBeacon b1;
   auto m1 = WLifetimeMonitor (b1);
   auto b2_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon (std::move (b1)));
   BOOST_CHECK (m1.IsAlive ());
   b2_p.reset ();
   BOOST_CHECK (!m1.IsAlive ());
}

BOOST_AUTO_TEST_CASE (beacon_move_operator)
{
   WLifetimeBeacon b1;
   auto m1 = WLifetimeMonitor (b1);
   auto b2_p = std::unique_ptr<WLifetimeBeacon>(new WLifetimeBeacon);
   auto m2 = WLifetimeMonitor (*b2_p);
   BOOST_CHECK (m1.IsAlive ());
   BOOST_CHECK (m2.IsAlive ());
   *b2_p = std::move (b1);
   BOOST_CHECK (m1.IsAlive ());
   BOOST_CHECK (!m2.IsAlive ());
   b2_p.reset ();
   BOOST_CHECK (!m1.IsAlive ());
   BOOST_CHECK (!m2.IsAlive ());
}

BOOST_AUTO_TEST_CASE (beacon_reset)
{
   auto b1 = WLifetimeBeacon ();
   auto m1 = WLifetimeMonitor (b1);
   BOOST_CHECK (m1.IsAlive ());
   b1.Reset ();
   BOOST_CHECK (!m1.IsAlive ());
   auto m2 = WLifetimeMonitor (b1);
   BOOST_CHECK (!m1.IsAlive ());
   BOOST_CHECK (m2.IsAlive ());
   b1.Reset ();
   BOOST_CHECK (!m1.IsAlive ());
   BOOST_CHECK (!m2.IsAlive ());   
}

BOOST_AUTO_TEST_SUITE_END(/*LifetimeMonitorTests*/)

