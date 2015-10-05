/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/Mailbox.h"
using namespace bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
using boost::bind;
using boost::ref;
#include <boost/test/unit_test.hpp>
#pragma warning (pop)

BOOST_AUTO_TEST_SUITE (MailBox)

BOOST_AUTO_TEST_CASE (ReadDefaultCtor)
{
   WMailBox<int> b1;
   BOOST_CHECK (!b1.CanRead ());
   BOOST_CHECK (!b1.WaitForReadable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (!b1.WaitForReadable (boost::posix_time::milliseconds (10)));
   BOOST_CHECK (!b1.Read (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (!b1.Read (boost::posix_time::milliseconds (10)));
   BOOST_CHECK (!b1.Peek ());

   struct AtomicTest
   {
      static void Read (WMailBox<int>& b, WAtomic& at)
      {
         BOOST_CHECK (!b.CanRead (at));
         BOOST_CHECK (!b.ReadAtomic (at));
         BOOST_CHECK (!b.Peek (at));
      }      
   };
   Atomically (bind (AtomicTest::Read, ref (b1), _1));

   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::ReadRetry, ref (b1), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotReadable, ref (b1), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (ReadValueCtor)
{
   const int value1 = 12373;
   WMailBox<int> b1 (value1);
   BOOST_CHECK (b1.CanRead ());
   BOOST_CHECK (b1.WaitForReadable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (b1.WaitForReadable (boost::posix_time::milliseconds (10)));
   WMailBox<int>::DataOpt op1;
   BOOST_CHECK_NO_THROW (op1 = b1.Peek ());
   BOOST_CHECK (op1);
   if (op1)
   {
      BOOST_CHECK_EQUAL (op1.get (), value1);
   }
   WMailBox<int>::DataOpt o1;
   BOOST_CHECK_NO_THROW (o1 = b1.Read (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (o1);
   if (o1)
   {
      BOOST_CHECK_EQUAL (o1.get (), value1);
   }
   //make sure that value1 was cleared
   BOOST_CHECK (!b1.CanRead ());
   BOOST_CHECK (!b1.WaitForReadable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (!b1.WaitForReadable (boost::posix_time::milliseconds (10)));
   BOOST_CHECK (!b1.Read (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (!b1.Read (boost::posix_time::milliseconds (10)));
   BOOST_CHECK (!b1.Peek ());

   struct AtomicTest
   {
      static void Read (WMailBox<int>& b, const int value, WAtomic& at)
      {
         BOOST_CHECK (b.CanRead (at));
         WMailBox<int>::DataOpt op1;
         BOOST_CHECK_NO_THROW (op1 = b.Peek (at));
         BOOST_CHECK (op1);
         if (op1)
         {
            BOOST_CHECK_EQUAL (op1.get (), value);
         }
         WMailBox<int>::DataOpt o1;
         BOOST_CHECK_NO_THROW (o1 = b.ReadAtomic (at));
         BOOST_CHECK (o1);
         if (o1)
         {
            BOOST_CHECK_EQUAL (o1.get (), value);
         }
         //make sure that value1 was cleared
         BOOST_CHECK (!b.CanRead (at));
         BOOST_CHECK (!b.Peek (at));
         BOOST_CHECK (!b.ReadAtomic (at));
      }      
   };
   const int value2 = 769129;
   WMailBox<int> b2 (value2);
   Atomically (bind (AtomicTest::Read, ref (b2), value2, _1));

   const int value3 = 989975;
   WMailBox<int> b3 (value3);
   BOOST_CHECK_NO_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotReadable, ref (b3), _1,
                                           boost::posix_time::milliseconds (1))));
   int res3 = -1;
   BOOST_CHECK_NO_THROW (res3 = Atomically (bind (&WMailBox<int>::ReadRetry, ref (b3), _1,
                                                  boost::posix_time::milliseconds (1))));
   BOOST_CHECK_EQUAL (res3, value3);
   BOOST_CHECK (!b3.CanRead ());
   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::ReadRetry, ref (b1), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotReadable, ref (b3), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (WriteWhenFull)
{
   const int value1 = 566144;
   WMailBox<int> b1 (value1);
   BOOST_CHECK (!b1.CanWrite ());
   BOOST_CHECK (!b1.WaitForWriteable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (!b1.WaitForWriteable (boost::posix_time::milliseconds (10)));
   BOOST_CHECK (!b1.Write (763482, boost::posix_time::milliseconds (0)));
   WMailBox<int>::DataOpt op1 = b1.Peek ();
   BOOST_CHECK (op1);
   if (op1)
   {
      BOOST_CHECK_EQUAL (value1, op1.get ());
   }
   
   struct AtomicTest
   {
      static void Write (WMailBox<int>& b, const int value, WAtomic& at)
      {
         BOOST_CHECK (!b.CanWrite (at));
         BOOST_CHECK (!b.WriteAtomic (933343, at));
         WMailBox<int>::DataOpt op1 = b.Peek (at);
         BOOST_CHECK (op1);
         if (op1)
         {
            BOOST_CHECK_EQUAL (value, op1.get ());
         }
      }
   };
   Atomically (bind (AtomicTest::Write, ref (b1), value1, _1));

   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::WriteRetry, ref (b1), 192458, _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
   WMailBox<int>::DataOpt op2 = b1.Peek ();
   BOOST_CHECK (op2);
   if (op2)
   {
      BOOST_CHECK_EQUAL (value1, op2.get ());
   }
   
   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotWritable, ref (b1), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
   WMailBox<int>::DataOpt op3 = b1.Peek ();
   BOOST_CHECK (op3);
   if (op3)
   {
      BOOST_CHECK_EQUAL (value1, op3.get ());
   }
}

BOOST_AUTO_TEST_CASE (WriteWhenEmpty)
{
   WMailBox<int> b1;
   BOOST_CHECK (b1.CanWrite ());
   BOOST_CHECK (b1.WaitForWriteable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK (b1.WaitForWriteable (boost::posix_time::milliseconds (10)));
   const int v1 = 144632;
   BOOST_CHECK (b1.Write (v1, boost::posix_time::milliseconds (0)));
   WMailBox<int>::DataOpt op1 = b1.Peek ();
   BOOST_CHECK (op1);
   if (op1)
   {
      BOOST_CHECK_EQUAL (v1, op1.get ());
   }
   b1.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (b1.CanWrite ());

   struct AtomicTest
   {
      static void Write (WMailBox<int>& b, const int value, WAtomic& at)
      {
         BOOST_CHECK (b.CanWrite (at));
         BOOST_CHECK (b.WriteAtomic (value, at));
         WMailBox<int>::DataOpt op1 = b.Peek (at);
         BOOST_CHECK (op1);
         if (op1)
         {
            BOOST_CHECK_EQUAL (value, op1.get ());
         }
         BOOST_CHECK (!b.CanWrite (at));
      }
   };
   const int v2 = 348322;
   Atomically (bind (AtomicTest::Write, ref (b1), v2, _1));
   BOOST_CHECK (!b1.CanWrite ());
   WMailBox<int>::DataOpt op2 = b1.Read ();
   BOOST_CHECK (op2);
   if (op2)
   {
      BOOST_CHECK_EQUAL (v2, op2.get ());
   }
   BOOST_CHECK (b1.CanWrite ());

   BOOST_CHECK_NO_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotWritable, ref (b1), _1,
                                           boost::posix_time::milliseconds (1))));
   
   const int v3 = 323849;
   BOOST_CHECK_NO_THROW (Atomically (bind (&WMailBox<int>::WriteRetry, ref (b1), v3, _1,
                                           boost::posix_time::milliseconds (1))));
   WMailBox<int>::DataOpt op3 = b1.Peek ();
   BOOST_CHECK (op3);
   if (op3)
   {
      BOOST_CHECK_EQUAL (v3, op3.get ());
   }
}

BOOST_AUTO_TEST_CASE (BoundReadWrongThread)
{
   struct BindReadThread
   {
      static void Run (WMailBox<int>& b)
      {
         b.BindReadToThread ();
      }
   };

   WMailBox<int> b;
   boost::thread t (bind (BindReadThread::Run, ref (b)));
   t.join ();
   BOOST_CHECK_THROW (b.CanRead (), WMailBoxBadThreadError);
   BOOST_CHECK_THROW (b.WaitForReadable (boost::posix_time::milliseconds (0)), WMailBoxBadThreadError);
   BOOST_CHECK_THROW (b.Read (boost::posix_time::milliseconds (0)), WMailBoxBadThreadError);
   BOOST_CHECK_THROW (b.Peek (), WMailBoxBadThreadError);

   struct AtomicTest
   {
      static void Run (WMailBox<int>& b, WAtomic& at)
      {
         BOOST_CHECK_THROW (b.CanRead (at), WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.RetryIfNotReadable (at, boost::posix_time::milliseconds (0)),
                            WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.ReadAtomic (at), WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.ReadRetry (at, boost::posix_time::milliseconds (0)),
                            WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.Peek (at), WMailBoxBadThreadError);         
      }
   };
   Atomically (bind (AtomicTest::Run, ref (b), _1));
}

BOOST_AUTO_TEST_CASE (BoundReadRightThread)
{
   WMailBox<int> b;
   b.BindReadToThread ();
   BOOST_CHECK_NO_THROW (b.CanRead ());
   BOOST_CHECK_NO_THROW (b.WaitForReadable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK_NO_THROW (b.Read (boost::posix_time::milliseconds (0)));
   BOOST_CHECK_NO_THROW (b.Peek ());

   struct AtomicTest
   {
      static void Run (WMailBox<int>& b, WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (b.CanRead (at));
         BOOST_CHECK_NO_THROW (b.ReadAtomic (at));
         BOOST_CHECK_NO_THROW (b.Peek (at));         
      }
   };
   Atomically (bind (AtomicTest::Run, ref (b), _1));

   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::ReadRetry, ref (b), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
   BOOST_CHECK_THROW (Atomically (bind (&WMailBox<int>::RetryIfNotReadable, ref (b), _1,
                                        boost::posix_time::milliseconds (1))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (BoundReadAlreadyBound)
{
   WMailBox<int> b;
   b.BindReadToThread ();
   BOOST_CHECK_THROW (b.BindReadToThread (), WMailBoxAlreadyBoundError);
}

BOOST_AUTO_TEST_CASE (BoundWriteWrongThread)
{
   struct BindWriteThread
   {
      static void Run (WMailBox<int>& b)
      {
         b.BindWriteToThread ();
      }
   };

   WMailBox<int> b;
   boost::thread t (bind (BindWriteThread::Run, ref (b)));
   t.join ();
   BOOST_CHECK_THROW (b.CanWrite (), WMailBoxBadThreadError);
   BOOST_CHECK_THROW (b.WaitForWriteable (boost::posix_time::milliseconds (0)),
                      WMailBoxBadThreadError);
   BOOST_CHECK_THROW (b.Write (0), WMailBoxBadThreadError);

   struct AtomicTest
   {
      static void Run (WMailBox<int>& b, WAtomic& at)
      {
         BOOST_CHECK_THROW (b.CanWrite (at), WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.RetryIfNotWritable (at, boost::posix_time::milliseconds (0)),
                            WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.WriteAtomic (0, at), WMailBoxBadThreadError);
         BOOST_CHECK_THROW (b.WriteRetry (0, at, boost::posix_time::milliseconds (0)),
                            WMailBoxBadThreadError);
      }
   };
   Atomically (bind (AtomicTest::Run, ref (b), _1));
}

BOOST_AUTO_TEST_CASE (BoundWriteRightThread)
{
   WMailBox<int> b;
   b.BindWriteToThread ();
   BOOST_CHECK_NO_THROW (b.CanWrite ());
   BOOST_CHECK_NO_THROW (b.WaitForWriteable (boost::posix_time::milliseconds (0)));
   BOOST_CHECK_NO_THROW (b.Write (0, boost::posix_time::milliseconds (0)));
   b.Read ();
   
   struct AtomicTest
   {
      static void Run (WMailBox<int>& b, WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (b.CanWrite (at));
         BOOST_CHECK_NO_THROW (b.WriteAtomic (0, at));
         b.ReadAtomic (at);
         BOOST_CHECK_NO_THROW (b.WriteRetry (0, at, boost::posix_time::milliseconds (0)));
         b.ReadAtomic (at);
         BOOST_CHECK_NO_THROW (b.RetryIfNotWritable (at, boost::posix_time::milliseconds (0)));
      }
   };
   Atomically (bind (AtomicTest::Run, ref (b), _1));
}

BOOST_AUTO_TEST_CASE (BoundWriteAlreadyBound)
{
   WMailBox<int> b;
   b.BindWriteToThread ();
   BOOST_CHECK_THROW (b.BindWriteToThread (), WMailBoxAlreadyBoundError);
}

BOOST_AUTO_TEST_SUITE_END (/*MailBox*/)
