/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/ChannelFilter.h"
using namespace  bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/format.hpp>
using boost::format;
using boost::str;
#include <boost/optional.hpp>
using boost::optional;
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

namespace
{
   struct WOddOnly
   {
      bool operator()(const int& val)
      {
         return (val%2 == 1);
      }
   };

   struct WEvenOnly
   {
      bool operator()(const int& val)
      {
         return (val%2 == 0);
      }
   };
}

BOOST_AUTO_TEST_SUITE (ChannelFilterTests)

BOOST_AUTO_TEST_CASE (filter)
{
   WChannel<int> mainChan;
   WChannel<int> filteredChan;
   
   WChannelFilter<int> filter (mainChan, filteredChan, WOddOnly ());

   WChannelReader<int> mainReader (mainChan);
   WChannelReader<int> filteredReader (filteredChan);
   
   WOddOnly IsOdd;
   for (int i = 0; i < 10; ++i)
   {
      mainChan.Write (i);
      optional<int> mainVal_o = mainReader.Read (boost::posix_time::milliseconds (0));
      BOOST_CHECK_MESSAGE (mainVal_o, str (format ("i = %1%") % i));
      if (mainVal_o)
      {
         BOOST_CHECK_EQUAL (*mainVal_o, i);
      }

      optional<int> filteredVal_o = filteredReader.Read (boost::posix_time::milliseconds (0));
      if (IsOdd (i))
      {
         BOOST_CHECK_MESSAGE (filteredVal_o, str (format ("i = %1%") % i));
         if (filteredVal_o)
         {
            BOOST_CHECK_EQUAL (*filteredVal_o, i);
         }
      }
      else
      {
         BOOST_CHECK_MESSAGE (!filteredVal_o, str (format ("i = %1%") % i));
      }
   }
}

BOOST_AUTO_TEST_CASE (disconnect)
{
   WChannel<int> mainChan;
   WChannel<int> filteredChan;
   
   WChannelFilter<int> filter (mainChan, filteredChan, WOddOnly ());

   WChannelReader<int> filteredReader (filteredChan);

   mainChan.Write (0);
   filter.Disconnect ();
   mainChan.Write (1);
   BOOST_CHECK (!filteredReader.Read (boost::posix_time::milliseconds (0)));
}

BOOST_AUTO_TEST_CASE (reset)
{
   WChannel<int> mainChan;
   WChannel<int> filteredChan1;
   WChannel<int> filteredChan2;

   WChannelFilter<int> filter (mainChan, filteredChan1, WOddOnly ());

   WChannelReader<int> filteredReader1 (filteredChan1);
   WChannelReader<int> filteredReader2 (filteredChan2);

   mainChan.Write (1);
   optional<int> val1_o = filteredReader1.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (val1_o);
   if (val1_o)
   {
      BOOST_CHECK_EQUAL (*val1_o, 1);
   }

   filter.Reset (mainChan, filteredChan2, WEvenOnly ());
   mainChan.Write (2);
   optional<int> val2_o = filteredReader2.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (val2_o);
   if (val2_o)
   {
      BOOST_CHECK_EQUAL (*val2_o, 2);
   }

   mainChan.Write (3);
   optional<int> val3_o = filteredReader2.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (!val3_o);
}

BOOST_AUTO_TEST_CASE (filter_gone)
{
   WChannel<int> mainChan;
   WChannel<int> filteredChan;
   
   WChannelFilter<int>::Ptr filter_p (new WChannelFilter<int> (mainChan, filteredChan, WOddOnly ()));

   WChannelReader<int> filteredReader (filteredChan);

   filter_p.reset ();
   mainChan.Write (1);
   BOOST_CHECK (filteredReader.Read ());
}

BOOST_AUTO_TEST_CASE (channel_gone)
{
   WChannel<int> mainChan;
   WChannelReader<int> mainReader (mainChan);
   boost::shared_ptr<WChannel<int> > filteredChan_p (new WChannel<int>);
   WChannelFilter<int>::Ptr filter_p (new WChannelFilter<int> (mainChan, *filteredChan_p, WOddOnly ()));
   filteredChan_p.reset ();
   mainChan.Write (1);
   //mostly making sure things don't crash here
   BOOST_CHECK (mainReader.Read (boost::posix_time::milliseconds (0)));   
}

BOOST_AUTO_TEST_SUITE_END (/*ChannelFilterTests*/)
