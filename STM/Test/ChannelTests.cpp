/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#pragma warning (disable: 4244)

#include "BSS/Thread/STM/Channel.h"

#include "BSS/Common/SequenceBuilder.h"

using bss::SequenceBuilder::seq;
using bss::SequenceBuilder::vb;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/thread/thread.hpp>
using boost::thread;
#include <boost/thread/barrier.hpp>
using boost::barrier;
#pragma warning (pop)

#include <vector>

using namespace bss;
using namespace bss::thread;
using namespace bss::thread::STM;

BOOST_AUTO_TEST_SUITE (Channel)

namespace
{
   typedef boost::shared_ptr<barrier> WBarrierPtr;
            
   struct WTestMsg
   {
      WTestMsg (): m_code (-1) {}
      WTestMsg (int code) : m_code (code) {}
      int m_code;

      bool operator== (const WTestMsg& msg) const {return m_code == msg.m_code;}
   };

   std::ostream& operator<< (std::ostream& out, const WTestMsg& msg)
   {
      out << "WTestMsg (" << msg.m_code << ")";
      return out;
   }
            
   void HandleWrite (bool& gotSignal)
   {
      gotSignal = true;
   }
}

BOOST_AUTO_TEST_CASE (invalid_channel_test_message)
{
   WInvalidChannelError err;
   BOOST_CHECK (err.m_msg == "Attempt to use an invalid channel");
}

BOOST_AUTO_TEST_CASE (test_ctor)
{
   BOOST_CHECK_NO_THROW (WChannel<WTestMsg> ());
}

BOOST_AUTO_TEST_CASE (test_write)
{
   WChannel<WTestMsg> chan;
   BOOST_CHECK_NO_THROW (chan.Write (WTestMsg (1)));
}

BOOST_AUTO_TEST_CASE (test_writeAtomic)
{
   struct WAtomicWrite
   {
      typedef void result_type;
      void operator () (WChannel<WTestMsg>& chan, WAtomic& at) const
      {
         BOOST_CHECK_NO_THROW (chan.Write (WTestMsg (1), at));                  
      }
   };

   WChannel<WTestMsg> chan;
   Atomically (boost::bind (WAtomicWrite (), boost::ref (chan), _1));
}

BOOST_AUTO_TEST_CASE (test_writeSignal)
{
   bool gotSignal = false;
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);//need reader, else signal not emitted
   chan.writeSignal.Connect (boost::bind (HandleWrite, boost::ref (gotSignal)));
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);

   gotSignal = false;
   struct WAtomicWrite
   {
      static void Run (WChannel<WTestMsg>& chan, const WTestMsg& msg, WAtomic& at)
      {
         chan.Write (msg, at);
      }
   };
   Atomically (boost::bind (WAtomicWrite::Run, boost::ref (chan), WTestMsg (2), _1));
   BOOST_CHECK (gotSignal);

   gotSignal = false;
   bool gotSignal2 = false;
   bss::thread::ThreadSafeSignal::WConnection conn =
      chan.writeSignal.Connect (boost::bind (HandleWrite, boost::ref (gotSignal2)));
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);
   BOOST_CHECK (gotSignal2);
   conn.Disconnect ();
   gotSignal = false;
   gotSignal2 = false;            
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);
   BOOST_CHECK (!gotSignal2);
}

BOOST_AUTO_TEST_CASE (test_defaultCtorReadOnly)
{
   BOOST_CHECK_NO_THROW (WReadOnlyChannel<int> ro);
}

BOOST_AUTO_TEST_CASE (test_ctorReadOnly)
{
   WChannel<int> chan;
   BOOST_CHECK_NO_THROW (WReadOnlyChannel<int> ro (chan));
   WReadOnlyChannel<int> ro1 (chan);
   BOOST_CHECK_NO_THROW (WReadOnlyChannel<int> ro2 (ro1));
}

BOOST_AUTO_TEST_CASE (test_initReadOnly)
{
   WChannel<int> chan;
   WReadOnlyChannel<int> ro;
   BOOST_CHECK_NO_THROW (ro.Init (chan));            
   WReadOnlyChannel<int> ro2;
   BOOST_CHECK_NO_THROW (ro2.Init (ro));            
}

BOOST_AUTO_TEST_CASE (test_releaseReadOnly)
{
   WChannel<int> chan;
   WReadOnlyChannel<int> ro (chan);
   ro.Release ();
   BOOST_CHECK (!ro);
}
         
BOOST_AUTO_TEST_CASE (test_operatorBoolReadOnly)
{
   WReadOnlyChannel<int> ro;
   BOOST_CHECK (!ro);
   WChannel<int> chan;
   ro.Init (chan);
   BOOST_CHECK (ro);

   WReadOnlyChannel<int> ro2 (chan);
   BOOST_CHECK (ro);

#pragma warning (push)
#pragma warning (disable : 4127)
   if (true)
   {
      WChannel<int> chan2;
      ro.Init (chan2);
   }
   BOOST_CHECK (!ro);

   boost::shared_ptr<WReadOnlyChannel<int> > ro_p;
   if (true)
   {
      WChannel<int> chan2;
      ro_p.reset (new WReadOnlyChannel<int> (chan2));
   }
   BOOST_CHECK (! (*ro_p));
#pragma warning (pop)
}

BOOST_AUTO_TEST_CASE (test_writeSignalReadOnly)
{
   bool gotSignal = false;
   WChannel<WTestMsg> chan;
   WReadOnlyChannel<WTestMsg> roChan (chan);
   roChan.ConnectToWriteSignal (boost::bind (HandleWrite, boost::ref (gotSignal)));
   WChannelReader<WTestMsg> reader (roChan);//need reader, else signal not emitted
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);

   gotSignal = false;
   struct WAtomicWrite
   {
      static void Run (WChannel<WTestMsg>& chan, const WTestMsg& msg, WAtomic& at)
      {
         chan.Write (msg, at);
      }
   };
   Atomically (boost::bind (WAtomicWrite::Run, boost::ref (chan), WTestMsg (2), _1));
   BOOST_CHECK (gotSignal);            

   gotSignal = false;
   bool gotSignal2 = false;
   bss::thread::ThreadSafeSignal::WConnection conn =
      roChan.ConnectToWriteSignal (boost::bind (HandleWrite, boost::ref (gotSignal2)));
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);
   BOOST_CHECK (gotSignal2);
   conn.Disconnect ();
   gotSignal = false;
   gotSignal2 = false;            
   chan.Write (WTestMsg (1));
   BOOST_CHECK (gotSignal);
   BOOST_CHECK (!gotSignal2);
}

BOOST_AUTO_TEST_CASE (test_defaultCtorWriter)
{
   BOOST_CHECK_NO_THROW (WChannelWriter<int> w);
}

BOOST_AUTO_TEST_CASE (test_ctorWriter)
{
   WChannel<int> chan;
   BOOST_CHECK_NO_THROW (WChannelWriter<int> w (chan));
}

BOOST_AUTO_TEST_CASE (test_initWriter)
{
   WChannel<int> chan;
   WChannelWriter<int> w;
   BOOST_CHECK_NO_THROW (w.Init (chan));
}

BOOST_AUTO_TEST_CASE (test_releaseWriter)
{
   WChannelWriter<int> w;
   BOOST_CHECK_NO_THROW (w.Release ());
   WChannel<int> chan;
   w.Init (chan);
   BOOST_CHECK_NO_THROW (w.Release ());
   WChannelWriter<int> w2 (chan);
   BOOST_CHECK_NO_THROW (w2.Release ());
}

BOOST_AUTO_TEST_CASE (test_operatorBoolWriter)
{
   WChannelWriter<int> w;
   BOOST_CHECK (!w);
   WChannel<int> chan;
   w.Init (chan);
   BOOST_CHECK (w);
   w.Release ();
   BOOST_CHECK (!w);
   WChannelWriter<int> w2 (chan);
   BOOST_CHECK (w2);
   w2.Release ();
   BOOST_CHECK (!w2);

#pragma warning (push)
#pragma warning (disable : 4127)
   if (true)
   {
      WChannel<int> chan2;
      w.Init (chan2);
   }
   BOOST_CHECK (!w);

   boost::shared_ptr<WChannelWriter<int> > w_p;
   if (true)
   {
      WChannel<int> chan2;
      w_p.reset (new WChannelWriter<int> (chan2));
   }
   BOOST_CHECK (! (*w_p));
#pragma warning (pop)
}

BOOST_AUTO_TEST_CASE (test_writeWriter)
{
   WChannel<int> chan;
   WChannelWriter<int> w (chan);
   WChannelReader<int> r (chan); //need reader, else signal not emitted
   bool gotSignal = false;
   chan.writeSignal.Connect (boost::bind (HandleWrite, boost::ref (gotSignal)));
   BOOST_CHECK_NO_THROW (w.Write (0));
   BOOST_CHECK (gotSignal);
}

BOOST_AUTO_TEST_CASE (test_writeAtomicWriter)
{
   WChannel<int> chan;
   WChannelWriter<int> w (chan);
   WChannelReader<int> r (chan); //need reader, else signal not emitted
   bool gotSignal = false;
   chan.writeSignal.Connect (boost::bind (HandleWrite, boost::ref (gotSignal)));
   struct WAtomicWrite
   {
      static void Run (WChannelWriter<int>& chan, const int msg, WAtomic& at)
      {
         chan.Write (msg, at);
      }
   };
   Atomically (boost::bind (WAtomicWrite::Run, boost::ref (w), 0, _1));
   BOOST_CHECK (gotSignal);
}

namespace
{
   template <typename Data_t>
   void CheckUninitializedReader (WChannelReader<Data_t>& reader)
   {
      BOOST_CHECK (!reader);
      BOOST_CHECK_THROW (reader.Wait (), WInvalidChannelError);
      BOOST_CHECK_THROW (reader.Peek (), WInvalidChannelError);
      BOOST_CHECK_THROW (reader.Read (), WInvalidChannelError);
      BOOST_CHECK_THROW (reader.ReadAll (), WInvalidChannelError);

      struct WTest
      {
         static void run (WChannelReader<int>& reader, WAtomic& at)
         {
            BOOST_CHECK_THROW (reader.RetryIfEmpty (at), WInvalidChannelError);
            BOOST_CHECK_THROW (reader.Peek (at), WInvalidChannelError);
            BOOST_CHECK_THROW (reader.ReadAtomic (at), WInvalidChannelError);
            BOOST_CHECK_THROW (reader.ReadRetry (at), WInvalidChannelError);
            BOOST_CHECK_THROW (reader.ReadAll (at), WInvalidChannelError);
         }
      };
      Atomically (boost::bind (WTest::run, boost::ref (reader), _1));
   }
}

BOOST_AUTO_TEST_CASE (test_defaultCtorReader)
{
   WChannelReader<int> reader;
   CheckUninitializedReader (reader);
}

namespace
{
   template <typename Data_t>
   void TestReader (WChannel<Data_t>& chan, WChannelReader<Data_t>& reader, Data_t val)
   {
      BOOST_CHECK (reader);
      chan.Write (val);
      boost::optional<Data_t> res = reader.Read ();
      BOOST_CHECK (res);
      BOOST_CHECK_EQUAL (val, res.get ());               
   }
}

BOOST_AUTO_TEST_CASE (test_channelCtor)
{
   WChannel<WTestMsg> chan;
   BOOST_CHECK_NO_THROW (WChannelReader<WTestMsg> reader (chan));

   WChannelReader<WTestMsg> reader (chan);
   TestReader (chan, reader, WTestMsg (774562));
            
   struct WAtomicCtor
   {
      static WChannelReader<WTestMsg>
      run (const WChannel<WTestMsg>& chan, WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (WChannelReader<WTestMsg> reader (chan, at));
         return WChannelReader<WTestMsg> (chan, at);
      }
   };
   WChannel<WTestMsg> chan2;            
   WChannelReader<WTestMsg> reader2 =
      Atomically (boost::bind (WAtomicCtor::run, boost::cref (chan2), _1));
   TestReader (chan2, reader2, WTestMsg (398528));
}

BOOST_AUTO_TEST_CASE (test_readOnlyChannelCtor)
{
   WReadOnlyChannel<WTestMsg> ro;
   BOOST_CHECK_THROW (WChannelReader<WTestMsg> reader (ro), WInvalidChannelError);
            
   WChannel<WTestMsg> chan;
   ro.Init (chan);
   BOOST_CHECK_NO_THROW (WChannelReader<WTestMsg> reader (ro));
   WChannelReader<WTestMsg> reader (ro);
   TestReader (chan, reader, WTestMsg (222343));
               
   struct WAtomicCtor
   {
      static WChannelReader<WTestMsg>
      run (const WReadOnlyChannel<WTestMsg>& chan, const bool valid, WAtomic& at)
      {
         if (valid)
         {
            BOOST_CHECK_NO_THROW (WChannelReader<WTestMsg> reader (chan, at));
            return WChannelReader<WTestMsg> (chan, at);
         }
         else
         {
            BOOST_CHECK_THROW (WChannelReader<WTestMsg> reader (chan, at), WInvalidChannelError);
            return WChannelReader<WTestMsg> ();
         }
      }
   };
   WReadOnlyChannel<WTestMsg> ro2;
   Atomically (boost::bind (WAtomicCtor::run, boost::cref (ro2), false, _1));
   WChannel<WTestMsg> chan2;
   ro2.Init (chan2);
   WChannelReader<WTestMsg> reader2 =
      Atomically (boost::bind (WAtomicCtor::run, boost::cref (ro2), true, _1));
   TestReader (chan2, reader2, WTestMsg (258384));
}

BOOST_AUTO_TEST_CASE (test_copyCtor)
{
   WChannelReader<int> original1;
   BOOST_CHECK_NO_THROW (WChannelReader<int> reader (original1));
   WChannelReader<int> reader1 (original1);
   CheckUninitializedReader (reader1);

   WChannel<int> chan2;
   WChannelReader<int> original2 (chan2);
   BOOST_CHECK_NO_THROW (WChannelReader<int> reader (original2));
   WChannelReader<int> reader2 (original2);
   TestReader (chan2, reader2, 256245);
            
   struct WAtomicCtor
   {
      static WChannelReader<int>
      run (const WChannelReader<int>& orig, WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (WChannelReader<int> reader (orig, at));
         return WChannelReader<int> (orig, at);
      }
   };
   WChannelReader<int> original3;
   WChannelReader<int> reader3 =
      Atomically (boost::bind (WAtomicCtor::run, boost::cref (original3), _1));
   CheckUninitializedReader (reader3);
   WChannel<int> chan4;
   WChannelReader<int> original4 (chan4);
   WChannelReader<int> reader4 =
      Atomically (boost::bind (WAtomicCtor::run, boost::cref (original4), _1));
   TestReader (chan4, reader4, 215995);
}
         
BOOST_AUTO_TEST_CASE (test_assignment)
{
   WChannelReader<int> original;
   WChannelReader<int> reader;
   BOOST_CHECK_NO_THROW (reader = original);
   CheckUninitializedReader (reader);

   WChannel<int> chan;
   WChannelReader<int> original2 (chan);
   BOOST_CHECK_NO_THROW (reader = original2);
   TestReader (chan, reader, 891416);
}

BOOST_AUTO_TEST_CASE (test_moveCtor)
{
   WChannelReader<int> original1;
   BOOST_CHECK_NO_THROW (WChannelReader<int> reader (original1));
   WChannelReader<int> reader1 (std::move (original1));
   CheckUninitializedReader (reader1);

   WChannel<int> chan2;
   WChannelReader<int> original2 (chan2);
   WChannelReader<int> reader2 (std::move (original2));
   TestReader (chan2, reader2, 256245);
}

BOOST_AUTO_TEST_CASE (test_move_operator)
{
   WChannelReader<int> original;
   WChannelReader<int> reader;
   BOOST_CHECK_NO_THROW (reader = std::move (original));
   CheckUninitializedReader (reader);

   WChannel<int> chan;
   WChannelReader<int> original2 (chan);
   BOOST_CHECK_NO_THROW (reader = std::move (original2));
   TestReader (chan, reader, 891416);
}

BOOST_AUTO_TEST_CASE (test_copyAtomic)
{
   struct WCopyAtomic
   {
      static void run (const WChannelReader<int>& orig,
                      WChannelReader<int>& reader,
                      WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (reader.Copy (orig, at));
      }
   };
   WChannelReader<int> original;
   WChannelReader<int> reader1;
   Atomically (boost::bind (WCopyAtomic::run, boost::cref (original), boost::ref (reader1), _1));
   CheckUninitializedReader (reader1);

   WChannel<int> chan;
   original.Init (chan);
   WChannelReader<int> reader2;
   Atomically (boost::bind (WCopyAtomic::run, boost::cref (original), boost::ref (reader2), _1));
   TestReader (chan, reader2, 828418);
}

BOOST_AUTO_TEST_CASE (test_initFromChannel)
{
   WChannel<int> chan;
   WChannelReader<int> reader;
   BOOST_CHECK_NO_THROW (reader.Init (chan));
   TestReader (chan, reader, 98411);
            
   struct Init
   {
      static void run (const WChannel<int>& chan,
                      WChannelReader<int>& reader,
                      WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (reader.Init (chan, at));                  
      }
   };
   WChannel<int> chan2;
   WChannelReader<int> reader2;
   Atomically (boost::bind (Init::run, boost::cref (chan2), boost::ref (reader2), _1));
   TestReader (chan2, reader2, 536775);
}
         
BOOST_AUTO_TEST_CASE (test_initFromWReadOnlyChannel)
{
   WReadOnlyChannel<int> ro;
   WChannelReader<int> reader;
   BOOST_CHECK_THROW (reader.Init (ro), WInvalidChannelError);
   WChannel<int> chan;
   ro.Init (chan);
   BOOST_CHECK_NO_THROW (reader.Init (ro));
   TestReader (chan, reader, 743089);

   struct Init
   {
      static void run (const WReadOnlyChannel<int>& ro,
                      WChannelReader<int>& reader,
                      WAtomic& at)
      {
         BOOST_CHECK_NO_THROW (reader.Init (ro, at));                  
      }
   };
   WChannel<int> chan2;
   WReadOnlyChannel<int> ro2 (chan2);
   WChannelReader<int> reader2;
   Atomically (boost::bind (Init::run, boost::cref (ro2), boost::ref (reader2), _1));
   TestReader (chan2, reader2, 600138);
            
   struct InitInvalid
   {
      static void run (const WReadOnlyChannel<int>& ro,
                      WChannelReader<int>& reader,
                      WAtomic& at)
      {
         BOOST_CHECK_THROW (reader.Init (ro, at), WInvalidChannelError);
      }
   };
   WReadOnlyChannel<int> ro3;
   WChannelReader<int> reader3;
   Atomically (boost::bind (InitInvalid::run, boost::cref (ro3), boost::ref (reader3), _1));
   CheckUninitializedReader (reader3);
}

BOOST_AUTO_TEST_CASE (test_releaseReadOnlyChannelReader)
{
   WChannelWriter<int> w;
   WChannelReader<int> reader1;
#pragma warning (push)
#pragma warning (disable : 4127)
   if (true)
   {
      WChannel<int> chan;
      w.Init (chan);
      reader1.Init (chan);
   }
   BOOST_CHECK (w); // make sure that reader still has
#pragma warning (pop)

   // the channel
   reader1.Release ();
   CheckUninitializedReader (reader1);
   BOOST_CHECK (!w); // make sure that reader released
   // the channel
            
   struct Release
   {
      static void run (WChannelReader<int>& reader, WAtomic& at)
      {
         reader.Release (at);
      }
   };

#pragma warning (push)
#pragma warning (disable : 4127)
   WChannelReader<int> reader2;
   if (true)
   {
      WChannel<int> chan;
      w.Init (chan);
      reader2.Init (chan);
   }
   BOOST_CHECK (w); // make sure that reader still has
#pragma warning (pop)

   // the channel
   Atomically (boost::bind (Release::run, boost::ref (reader2), _1));
   CheckUninitializedReader (reader2);
   BOOST_CHECK (!w); // make sure that reader released
   // the channel
}

BOOST_AUTO_TEST_CASE (test_operatorBoolReadOnlyChannelReader)
{
   WChannelReader<int> reader;
   BOOST_CHECK (!reader);
   WChannel<int> chan;
   reader.Init (chan);
   BOOST_CHECK (reader);
   reader.Release ();
   BOOST_CHECK (!reader);
}

BOOST_AUTO_TEST_CASE (test_valid)
{
   WChannelReader<int> reader;
   BOOST_CHECK (!Atomically (boost::bind (&WChannelReader<int>::Valid, boost::cref (reader), _1)));
   WChannel<int> chan;
   reader.Init (chan);
   BOOST_CHECK (Atomically (boost::bind (&WChannelReader<int>::Valid, boost::cref (reader), _1)));
   reader.Release ();
   BOOST_CHECK (!Atomically (boost::bind (&WChannelReader<int>::Valid, boost::cref (reader), _1)));
}

BOOST_AUTO_TEST_CASE (test_readInitiallyEmpty)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 1256;
   chan.Write (WTestMsg (VAL));
   Msg res = reader.Read ();
   bool haveMsg = res;
   BOOST_CHECK (haveMsg);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 987;
   chan.Write (WTestMsg (VAL2));
   res = reader.Read ();
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   res = reader.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (!res);
}

BOOST_AUTO_TEST_CASE (test_readInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int INIT_VAL = 49875;
   chan.Write (WTestMsg (INIT_VAL));
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 345;
   chan.Write (WTestMsg (VAL));
   Msg res = reader.Read ();
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 904875;
   chan.Write (WTestMsg (VAL2));
   res = reader.Read ();
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   res = reader.Read (boost::posix_time::milliseconds (0));
   BOOST_CHECK (!res);
}

BOOST_AUTO_TEST_CASE (test_readTimeoutInitiallyEmpty)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   Msg res = reader.Read (boost::posix_time::milliseconds (1));
   BOOST_CHECK (!res);

   const int VAL = 4953;
   chan.Write (WTestMsg (VAL));
   res = reader.Read (boost::posix_time::milliseconds (1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);
}

BOOST_AUTO_TEST_CASE (test_readTimeoutInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int INIT_VAL = 8397;
   chan.Write (WTestMsg (INIT_VAL));
   WChannelReader<WTestMsg> reader (chan);
   Msg res = reader.Read (boost::posix_time::milliseconds (1));
   BOOST_CHECK (!res);

   const int VAL = 1445;
   chan.Write (WTestMsg (VAL));
   res = reader.Read (boost::posix_time::milliseconds (1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);
}
         
BOOST_AUTO_TEST_CASE (test_readAtomicInitiallyEmpty)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 97;
   chan.Write (WTestMsg (VAL));
   Msg res =
      Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 34754;
   chan.Write (WTestMsg (VAL2));
   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (!res);
}

BOOST_AUTO_TEST_CASE (test_readAtomicInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int INIT_VAL = 398547;
   chan.Write (WTestMsg (INIT_VAL));
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 2435;
   chan.Write (WTestMsg (VAL));
   Msg res =
      Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 908433;
   chan.Write (WTestMsg (VAL2));
   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadAtomic, boost::ref (reader), _1));
   BOOST_CHECK (!res);
}

BOOST_AUTO_TEST_CASE (test_readRetryInitiallyEmpty)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 97;
   chan.Write (WTestMsg (VAL));
   Msg res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                      boost::ref (reader), _1, WTimeArg::UNLIMITED ()));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 34754;
   chan.Write (WTestMsg (VAL2));
   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                  boost::ref (reader), _1, WTimeArg::UNLIMITED ()));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   BOOST_CHECK_THROW (Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                               boost::ref (reader),
                                               _1,
                                               boost::posix_time::milliseconds (0))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (test_readRetryInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int INIT_VAL = 398547;
   chan.Write (WTestMsg (INIT_VAL));
   WChannelReader<WTestMsg> reader (chan);
   const int VAL = 2435;
   chan.Write (WTestMsg (VAL));
   Msg res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                      boost::ref (reader), _1, WTimeArg::UNLIMITED ()));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL, res->m_code);

   const int VAL2 = 908433;
   chan.Write (WTestMsg (VAL2));
   res = Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                  boost::ref (reader), _1, WTimeArg::UNLIMITED ()));
   BOOST_CHECK (res);
   BOOST_CHECK_EQUAL (VAL2, res->m_code);

   BOOST_CHECK_THROW (Atomically (boost::bind (&WChannelReader<WTestMsg>::ReadRetry,
                                               boost::ref (reader),
                                               _1,
                                               boost::posix_time::milliseconds (0))),
                      WRetryTimeoutException);
}

BOOST_AUTO_TEST_CASE (test_readAllInitiallyEmpty)
{
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   const std::vector<WTestMsg> v1 =
      seq (vb (WTestMsg (23423)) << WTestMsg (9876) << WTestMsg (293799));
   BOOST_FOREACH (const WTestMsg& msg, v1)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res1 = reader.ReadAll ();
   BOOST_CHECK (res1 == v1);
            
   const std::vector<WTestMsg> v2 =
      seq (vb (WTestMsg (9745)) << WTestMsg (2431));
   BOOST_FOREACH (const WTestMsg& msg, v2)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res2 = reader.ReadAll ();
   BOOST_CHECK (res2 == v2);

   const std::vector<WTestMsg> res3 = reader.ReadAll ();
   BOOST_CHECK (res3.empty ());
}

BOOST_AUTO_TEST_CASE (test_readAllInitiallyFull)
{
   WChannel<WTestMsg> chan;
   chan.Write (WTestMsg (4358));
   WChannelReader<WTestMsg> reader (chan);
   const std::vector<WTestMsg> v1 =
      seq (vb (WTestMsg (9735)) << WTestMsg (3245));
   BOOST_FOREACH (const WTestMsg& msg, v1)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res1 = reader.ReadAll ();
   BOOST_CHECK (res1 == v1);
            
   const std::vector<WTestMsg> v2 =
      seq (vb (WTestMsg (987354)) << WTestMsg (5648) << WTestMsg (3875927));
   BOOST_FOREACH (const WTestMsg& msg, v2)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res2 = reader.ReadAll ();
   BOOST_CHECK (res2 == v2);

   const std::vector<WTestMsg> res3 = reader.ReadAll ();
   BOOST_CHECK (res3.empty ());
}

BOOST_AUTO_TEST_CASE (test_readAllAtomicInitiallyEmpty)
{
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   const std::vector<WTestMsg> v1 =
      seq (vb (WTestMsg (23423)) << WTestMsg (9876) << WTestMsg (293799));
   BOOST_FOREACH (const WTestMsg& msg, v1)
   {
      chan.Write (msg);
   }
   struct WAtomicReadAll
   {
      static std::vector<WTestMsg> Run (WChannelReader<WTestMsg>& chan, WAtomic& at)
      {
         return chan.ReadAll (at);
      }
   };
   const std::vector<WTestMsg> res1 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res1 == v1);
            
   const std::vector<WTestMsg> v2 =
      seq (vb (WTestMsg (9745)) << WTestMsg (2431));
   BOOST_FOREACH (const WTestMsg& msg, v2)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res2 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res2 == v2);

   const std::vector<WTestMsg> res3 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res3.empty ());
}

BOOST_AUTO_TEST_CASE (test_readAllAtomicInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   chan.Write (WTestMsg (4358));
   WChannelReader<WTestMsg> reader (chan);
   const std::vector<WTestMsg> v1 =
      seq (vb (WTestMsg (9735)) << WTestMsg (3245));
   BOOST_FOREACH (const WTestMsg& msg, v1)
   {
      chan.Write (msg);
   }
   struct WAtomicReadAll
   {
      static std::vector<WTestMsg> Run (WChannelReader<WTestMsg>& chan, WAtomic& at)
      {
         return chan.ReadAll (at);
      }
   };
   const std::vector<WTestMsg> res1 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res1 == v1);
            
   const std::vector<WTestMsg> v2 =
      seq (vb (WTestMsg (987354)) << WTestMsg (5648) << WTestMsg (3875927));
   BOOST_FOREACH (const WTestMsg& msg, v2)
   {
      chan.Write (msg);
   }
   const std::vector<WTestMsg> res2 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res2 == v2);

   const std::vector<WTestMsg> res3 =
      Atomically (boost::bind (WAtomicReadAll::Run, boost::ref (reader), _1));
   BOOST_CHECK (res3.empty ());
}

BOOST_AUTO_TEST_CASE (test_peekInitiallyFull)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int VAL = 987354 ;
   WChannelReader<WTestMsg> reader (chan);
   chan.Write (WTestMsg (VAL));
   Msg peekVal = reader.Peek ();
   BOOST_CHECK (peekVal);
   BOOST_CHECK_EQUAL (VAL, peekVal->m_code);
   Msg readVal = reader.Read ();
   BOOST_CHECK (readVal);
   BOOST_CHECK_EQUAL (VAL, readVal->m_code);
   Msg peekVal2 = reader.Peek ();
   BOOST_CHECK (!peekVal2);
}

BOOST_AUTO_TEST_CASE (test_peekInitiallyEmpty)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   WChannelReader<WTestMsg> reader (chan);
   Msg peekVal = reader.Peek ();
   BOOST_CHECK (!peekVal);
}

BOOST_AUTO_TEST_CASE (test_peekAtomic)
{
   typedef WChannelReader<WTestMsg>::DataOpt Msg;
            
   WChannel<WTestMsg> chan;
   const int VAL = 987354 ;
   WChannelReader<WTestMsg> reader (chan);
   chan.Write (WTestMsg (VAL));
   struct WAtomicPeek
   {
      static Msg Run (WChannelReader<WTestMsg>& reader, WAtomic& at)
      {
         return reader.Peek (at);
      }
   };
   Msg peekVal = Atomically (boost::bind (WAtomicPeek::Run, boost::ref (reader), _1));
   BOOST_CHECK (peekVal);
   BOOST_CHECK_EQUAL (VAL, peekVal->m_code);
   Msg readVal = reader.Read ();
   BOOST_CHECK (readVal);
   BOOST_CHECK_EQUAL (VAL, readVal->m_code);
   Msg peekVal2 = Atomically (boost::bind (WAtomicPeek::Run, boost::ref (reader), _1));
   BOOST_CHECK (!peekVal2);
}

BOOST_AUTO_TEST_CASE (test_wait)
{
   typedef WChannelReader<int>::DataOpt Msg;

   struct WWaitThread
   {
      WWaitThread (int value) : m_value (value) {}

      boost::shared_ptr<WChannelReader<int> > CreateChannelReader (WChannel<int>& chan, WAtomic& at)
      {
         return boost::shared_ptr<WChannelReader<int> > (new WChannelReader<int> (chan, at));
      }
               
      void Run (WChannel<int>& chan, WBarrierPtr barrier_p)
      {
         boost::shared_ptr<WChannelReader<int> > reader_p =
            Atomically (boost::bind (&WWaitThread::CreateChannelReader,
                                     this,
                                     boost::ref (chan),
                                     _1));
         WChannelReader<int>& reader = *reader_p;
         barrier_p->wait ();
         barrier_p->wait ();
         reader.Wait ();
         Msg msg = reader.Read (boost::posix_time::milliseconds (0));
         BOOST_CHECK (msg);
         m_value = msg.get ();
         barrier_p->wait ();
      }

      int m_value;
   };

   //test write after wait starts
   const int INIT_VALUE = 3895;
   const int VALUE = 45987;
   WWaitThread thrObj (INIT_VALUE);
   BOOST_CHECK_EQUAL (INIT_VALUE, thrObj.m_value);
   WBarrierPtr barrier_p (new barrier (2));
   WChannel<int> chan;
   boost::thread thr (boost::bind (&WWaitThread::Run,
                                   boost::ref (thrObj),
                                   boost::ref (chan),
                                   barrier_p));
   barrier_p->wait ();
   barrier_p->wait ();
   boost::this_thread::sleep (boost::posix_time::milliseconds (100));
   chan.Write (VALUE);
   barrier_p->wait ();
   BOOST_CHECK_EQUAL (VALUE, thrObj.m_value);
   thr.join ();

   //test write before wait starts
   thrObj.m_value = INIT_VALUE;
   const int VALUE2 = 59847;
   boost::thread thr2 (boost::bind (&WWaitThread::Run,
                                    boost::ref (thrObj),
                                    boost::ref (chan),
                                    barrier_p));
   barrier_p->wait ();
   chan.Write (VALUE2);
   barrier_p->wait ();
   barrier_p->wait ();
   BOOST_CHECK_EQUAL (VALUE2, thrObj.m_value);
   thr2.join ();
}

BOOST_AUTO_TEST_CASE (test_waitTimeout)
{
   typedef WChannelReader<int>::DataOpt Msg;

   struct WWaitThread
   {
      WWaitThread (int value,
                  int timeoutValue,
                  unsigned int timeout) :
         m_value (value), m_timeoutValue (timeoutValue), m_timeout (timeout)
      {}
               
      void Run (WChannel<int>& chan, WBarrierPtr barrier_p)
      {
         WChannelReader<int> reader (chan);
         barrier_p->wait ();
         if (reader.Wait (boost::posix_time::milliseconds (m_timeout)))
         {
            Msg msg = reader.Read (boost::posix_time::milliseconds (0));
            BOOST_CHECK (msg);
            m_value = msg.get ();                     
         }
         else
         {
            m_value = m_timeoutValue;
         }
         barrier_p->wait ();
      }

      int m_value;
      const int m_timeoutValue;
      const unsigned int m_timeout;

   private:
      WWaitThread& operator= (const WWaitThread&) { return *this; }
   };

   const int INIT_VALUE = 598;
   const int VALUE = 324523;
   const int TIMEOUT_VALUE = 43958;
   const unsigned int TIMEOUT = 50;
   WWaitThread thrObj (INIT_VALUE, TIMEOUT_VALUE, TIMEOUT);
   BOOST_CHECK_EQUAL (INIT_VALUE, thrObj.m_value);
   BOOST_CHECK_EQUAL (TIMEOUT_VALUE, thrObj.m_timeoutValue);
   BOOST_CHECK_EQUAL (TIMEOUT, thrObj.m_timeout);
   WBarrierPtr barrier_p (new barrier (2));
   WChannel<int> chan;
   boost::thread thr (boost::bind (&WWaitThread::Run,
                                   boost::ref (thrObj),
                                   boost::ref (chan),
                                   barrier_p));
   barrier_p->wait ();
   boost::this_thread::sleep (boost::posix_time::milliseconds (1));
   chan.Write (VALUE);
   barrier_p->wait ();
   BOOST_CHECK_EQUAL (VALUE, thrObj.m_value);
   thr.join ();

   boost::thread thr2 (boost::bind (&WWaitThread::Run,
                                    boost::ref (thrObj),
                                    boost::ref (chan),
                                    barrier_p));
   barrier_p->wait ();
   barrier_p->wait ();
   BOOST_CHECK_EQUAL (TIMEOUT_VALUE, thrObj.m_value);
   thr2.join ();
}

BOOST_AUTO_TEST_CASE (test_waitRetry)
{
   struct WWaitRetry
   {
      void Run (WChannelReader<int>& reader, WAtomic& at) const
      {
         reader.RetryIfEmpty (at);
      }
   };

   WChannel<int> chan;
   WChannelReader<int> reader (chan);
   BOOST_CHECK_THROW ((Atomically (boost::bind (&WWaitRetry::Run,
                                                WWaitRetry (),
                                                boost::ref (reader),
                                                _1),
                                   WConRes (WConflictResolution::THROW)
                                   & WMaxRetries (0))),
                      WMaxRetriesException);
}

BOOST_AUTO_TEST_CASE (test_waitRetryTimeout)
{
   struct WWaitRetry
   {
      void Run (WChannelReader<int>& reader, const unsigned int timeout, WAtomic& at) const
      {
         reader.RetryIfEmpty (at, boost::posix_time::milliseconds (timeout));
      }
   };

   WChannel<int> chan;
   WChannelReader<int> reader (chan);
   BOOST_CHECK_THROW ((Atomically (boost::bind (&WWaitRetry::Run,
                                                WWaitRetry (),
                                                boost::ref (reader),
                                                1,
                                                _1))),
                      WRetryTimeoutException);
}

namespace
{
   int InitialMessage (WAtomic&)
   {
      return -1;
   }
}

BOOST_AUTO_TEST_CASE (test_readerInitFunc)
{
   WChannel<int> chan (InitialMessage);

   WChannelReader<int> r1 (chan);
   const std::vector<int> v1 = r1.ReadAll ();
   BOOST_CHECK_EQUAL (1, static_cast<int> (v1.size ()));
   BOOST_CHECK_EQUAL (-1, v1[0]);

   WChannelReader<int> r2 (chan);
   chan.Write (0);
   const std::vector<int> v2 = r2.ReadAll ();
   BOOST_CHECK_EQUAL (2, static_cast<int> (v2.size ()));
   BOOST_CHECK_EQUAL (-1, v2[0]);
   BOOST_CHECK_EQUAL (0, v2[1]);

   WReadOnlyChannel<int> ro (chan);
            
   WChannelReader<int> r3 (ro);
   const std::vector<int> v3 = r3.ReadAll ();
   BOOST_CHECK_EQUAL (1, static_cast<int> (v3.size ()));
   BOOST_CHECK_EQUAL (-1, v3[0]);

   WChannelReader<int> r4 (ro);
   chan.Write (0);
   const std::vector<int> v4 = r4.ReadAll ();
   BOOST_CHECK_EQUAL (2, static_cast<int> (v4.size ()));
   BOOST_CHECK_EQUAL (-1, v4[0]);
   BOOST_CHECK_EQUAL (0, v4[1]);
}

BOOST_AUTO_TEST_CASE (test_setReaderInitFunc)
{
   WChannel<int> chan;
   chan.SetReaderInitFunc (InitialMessage);

   WChannelReader<int> r1 (chan);
   const std::vector<int> v1 = r1.ReadAll ();
   BOOST_CHECK_EQUAL (1, static_cast<int> (v1.size ()));
   BOOST_CHECK_EQUAL (-1, v1[0]);
}

BOOST_AUTO_TEST_CASE (test_stack_overflow_in_dtor)
{
   //Make sure that we don't get a stack overflow when the last reader
   //goes away and there are lots of unprocessed messages.
#pragma warning (push)
#pragma warning (disable : 4127)
   if (true)
   {
      WChannel<int> chan;
      WChannelReader<int> r1 (chan);
      for (int i = 0; i < 10000; ++i)
      {
         chan.Write (i);
      }
   }
   //if we get here then the test passed.
   BOOST_CHECK (true);
#pragma warning (pop)
}

BOOST_AUTO_TEST_CASE (test_stack_overflow_when_no_readers)
{
   WChannel<int> chan;
   Atomically ([&](WAtomic& at)
               {
                  for (int i = 0; i < 10000; ++i)
                  {
                     chan.Write (i, at);
                  }
               });
   //if we get here without a stack overflow then the test passed
   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (test_stack_overflow_read_atomic_lots)
{
   WChannel<int> chan;
   WChannelReader<int> reader (chan);
   Atomically ([&](WAtomic& at)
               {
                  for (int i = 0; i < 2500; ++i)
                  {
                     chan.Write (i, at);
                  }
               });
   Atomically ([&](WAtomic& at)
               {
                  reader.ReadAll (at);
               });
   //if we get here without a stack overflow then the test passed
   BOOST_CHECK (true);
}

BOOST_AUTO_TEST_CASE (test_stack_overflow_read_retry_lots)
{
   WChannel<int> chan;
   WChannelReader<int> reader (chan);
   auto maxElement = -1;
   Atomically ([&](WAtomic& at)
               {
                  for (int i = 0; i < 2500; ++i)
                  {
                     chan.Write (i, at);
                     maxElement = i;
                  }
               });
   auto lastElement = -1;
   try
   {
      //NOTE: We need this transaction to commit in order to actually test the stack overflow so we
      //don't read until we get the retry, we just read all the messages that we know are there
      Atomically ([&](WAtomic& at)
                  {
                     for (auto i = 0; i <= maxElement; ++i)
                     {
                        reader.ReadRetry (at, boost::posix_time::seconds (0));
                        lastElement = i;
                     }
                  });
   }
   catch(WRetryTimeoutException&)
   {
      //Ooops, read too many messages from the channel
      BOOST_ERROR ("Shouldn't have retried");
   }
   //We should have read everything
   BOOST_CHECK_EQUAL (lastElement, maxElement);
}

BOOST_AUTO_TEST_SUITE_END (/*Channel*/)
