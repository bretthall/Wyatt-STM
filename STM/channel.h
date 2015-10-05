/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "BSS/wtcbss.h"
#include "BSS/Common/Pointers.h"
#include "BSS/Thread/STM/stm.h"
#include "BSS/Thread/ThreadSafeSignal.h"

#ifdef WIN32
#pragma warning (push)
#pragma warning (disable: 4127 4239 4244 4265 4503 4512 4640 6011)
#endif

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/optional/optional.hpp>
#include <boost/function.hpp>

#include <vector>

namespace bss { namespace thread { namespace STM
{
   namespace  Internal
   {
      struct WChannelCoreNodeBase
      {};

      void BSS_LIBAPI IncrementNumNodes (WChannelCoreNodeBase* node_p);
      void BSS_LIBAPI DecrementNumNodes (WChannelCoreNodeBase* node_p);
      int BSS_LIBAPI GetNumNodes ();
      std::vector<size_t> BSS_LIBAPI GetExistingNodeNums ();
      size_t BSS_LIBAPI GetMaxNodeNum ();
      
      template <typename Data_t>
      struct WChannelCore
      {
         DEFINE_POINTERS (WChannelCore);

         typedef boost::function<Data_t (WAtomic&)> ReaderInitFunc;
         
         struct WNode : public WChannelCoreNodeBase
         {
            DEFINE_POINTERS (WNode);

            Data_t m_data;
            WVar<Ptr> m_next_v;
            bool m_initial;
               
            WNode (const Data_t& data):
               m_data (data),
               m_initial (false)
            {
               IncrementNumNodes (this);
            }

            static Ptr CreateInitialNode (const Data_t& data, const Ptr& next_p, WAtomic& at)
            {
               auto node_p = boost::make_shared<WNode> (data);
               node_p->m_next_v.Set (next_p, at);
               node_p->m_initial = true;
               return node_p;
            }
               
            WNode ():
               m_initial (false)
            {
               IncrementNumNodes (this);
            }

            ~WNode ()
            {
               DecrementNumNodes (this);
            }
         };

         typedef bss::thread::ThreadSafeSignal::WSignal<void ()> WWriteSignal;

         boost::shared_ptr<WWriteSignal> m_writeSignal_p;
         WVar<boost::shared_ptr<WNode>> m_next_v;
         ReaderInitFunc m_readerInit;
         WVar<int> m_numReaders_v;
         
         WChannelCore (ReaderInitFunc readerInit):
            m_writeSignal_p (boost::make_shared<WWriteSignal>()),
            m_next_v (boost::make_shared<WNode> ()),
            m_readerInit (readerInit),
            m_numReaders_v (0)
         {}

         struct WEmitSignal
         {
            boost::weak_ptr<WWriteSignal> m_sig_p;
            WEmitSignal (const boost::shared_ptr<WWriteSignal>& sig_p): m_sig_p (sig_p) {}

            void operator () ()
            {
               boost::shared_ptr<WWriteSignal> sig_p = m_sig_p.lock ();
               if (sig_p)
               {
                  (*sig_p) ();
               }
            }
         };

         void Write (const Data_t& data, WAtomic& at)
         {
            if (m_numReaders_v.Get (at) == 0)
            {
               //This is not just a performance optimization, if we build up a list of new nodes
               //when there aren't any readers then we can get a stack overflow. When the
               //transaction commits and there are no readers then the head node of the list of new
               //nodes will be dropped triggering a cascade of shared_ptr destrucors overflowing the
               //stack. So if there no readers we just drop the messages here. 
               return;
            }

            auto newNode_p = boost::make_shared<WNode> (data);
            auto cur_p = m_next_v.Get (at);
            if (cur_p)
            {
               cur_p->m_next_v.Set (newNode_p, at);
            }
            m_next_v.Set (newNode_p, at);
            at.After (WEmitSignal (m_writeSignal_p));
         }

         boost::shared_ptr<WNode> AddReader (WAtomic& at)
         {
            m_numReaders_v.Set (m_numReaders_v.Get (at) + 1, at);
            
            auto next_p = m_next_v.Get (at);
            if (m_readerInit)
            {
               return WNode::CreateInitialNode (m_readerInit (at), next_p, at);
            }
            else
            {
               return next_p;
            }
         }

         void RemoveReader (WAtomic& at)
         {
            const auto numReaders = m_numReaders_v.Get (at);
            if (numReaders > 0)
            {
               m_numReaders_v.Set (numReaders - 1, at);
            }
#ifdef _DEBUG
            else
            {
               //we can't assert until the transaction commits, we might be in an inconsistent state
               //that will be rolled back 
               at.After ([numReaders](){assert (numReaders > 0);});
            }
#endif //_DEBUG
         }
      };

   }

   /**
    * Base class for exceptions thrown by WChannel functions.
    */
   struct BSS_CLASSAPI WChannelError
   {
      WChannelError (const std::string& msg);
      std::string m_msg;
   };

   /**
      Exception thrown if an invalid channel is used.
   */
   struct BSS_CLASSAPI WInvalidChannelError : public WChannelError
   {
      WInvalidChannelError ();
   };

   /**
      The write end of a transactional multi-cast channel.  Messages
      are written into the channel using the write method of this
      class. The messages are then received by WChannelReader<Data_t>
      objects. Write and read operation are protected within STM
      transactions.
         
      @param Data_t The type of data sent through the channel. This
      type must be copyable. Normally this should be a
      boost::shared_ptr<const Val_t> where Val_t is the actual data
      type being sent. Note that all readers of the channel will get
      the same copy of the data, so if the data is mutable you could
      run into problems.
   */
   template <typename Data_t>
   class WChannel : public boost::noncopyable
   {
      template <typename> friend class WReadOnlyChannel;
      template <typename> friend class WChannelWriter;
      template <typename> friend class WChannelReader;

      typedef Internal::WChannelCore<Data_t> Core;
      typedef typename Core::Ptr CorePtr;
      CorePtr m_core_p;
         
   public:
      //!The type of data sent through this channel.
      typedef Data_t Data;

      /**
       * Intial message generation function. This function will be
       * called for each WChannelReader object created for a
       * channel. It is called within a STM transaction, the WAtomic
       * object is passed in. The returned value is set as the first
       * message that the new WChannelReader object sees, only the new
       * WChannelReader object will see this message.
       */
      typedef boost::function<Data_t (WAtomic&)> WReaderInitFunc;
         
      /**
         Creates an empty channel.

         @param m_readerInit The reader initialization function. This
         function generates the first message sent to a new reader. It
         is called once for each reader that is created from this
         channel at the time that the reader is being created. If it
         is invalid then no initial message will be sent to new
         readers.
      */
      WChannel (WReaderInitFunc m_readerInit = WReaderInitFunc ()):
         m_core_p (stm_new Internal::WChannelCore<Data_t> (m_readerInit)),
         writeSignal (* (m_core_p->m_writeSignal_p))
      {}

      WChannel (WChannel&& c):
         m_core_p (std::move (c.m_core_p)),
         writeSignal (* (m_core_p->m_writeSignal_p))
      {}

      WChannel& operator=(WChannel&& c)
      {
         m_core_p = std::move (c.m_core_p);
         writeSignal.Init (*(m_core_p->m_writeSignal_p));
         return *this;
      }
      
      /**
         Sets the reader initialization function for this channel
         replacing any existing reader initialization function.
            
         @param readerInit The reader initialization function. This
         function generates the first message sent to a new reader. It
         is called once for each reader that is created from this
         channel at the time that the reader is being created. If it
         is invalid then no initial message will be sent to new
         readers.
      */
      void SetReaderInitFunc (WReaderInitFunc readerInit)
      {
         m_core_p->m_readerInit = readerInit;
      }

      /**
         Writes a message to the channel.
          
         @param data The message to write.
          
         @param at The transaction to write the message within.
      */
      void Write (const Data& data, WAtomic& at)
      {
         m_core_p->Write (data, at);
      }

      /**
         Writes a message to the channel.  This version creates its
         own transaction to do the write operation within.  If you are
         already inside a transaction then WriteAtomic should be used
         instead.
      */
      void Write (const Data& data)
      {
         Atomically ([&](WAtomic& at){m_core_p->Write (data, at);});
      }
         
      //@{
      /**
         Signal emitted when the channel is written to.  This signal
         mainly exists so that WIN32 GUI code can receive window
         messages when a channel has something in it to read.  If you
         need to wait for a channel to be written to you are better
         off using WChannelReader::RetryIfEmpty.
      */
      typedef bss::thread::ThreadSafeSignal::WSignal<void ()> WWriteSignal;
      bss::thread::ThreadSafeSignal::WSignalConnector<WWriteSignal> writeSignal;
      //@}
   };

   /**
      Read only version of a WChannel.  The only thing that this
      class is good for is creating WChannelReader objects and
      connecting to the channel's "data written" signal.  This
      class is used when you have a channel that should only be
      writable from one class.  In this case you put a WChannel in
      the class' private data and a WReadOnlyChannel wrapping the
      private channel object in the public interface of the
      class.  Note that objects of this class are only useful for
      as long as the underlying WChannel object either exists or
      has readers.  Once neither of those conditions is met
      WReadOnlyChannel object can no longer be used.
   */
   template <typename Data_t>
   class WReadOnlyChannel
   {
      template <typename> friend class WChannelReader;

   public:
      typedef Data_t Data;
         
      /**
         Creates an uninitialized object.  Do not try to use the
         object until init has been called.
      */
      WReadOnlyChannel ()
      {}
         
      /**
         Creates and initializes a WReadOnlyChannel object.

         @param chan The channel that this object is wrapping.
      */
      WReadOnlyChannel (const WChannel<Data>& chan)
      {
         Init (chan);
      }

      /**
         Creates and initializes a WReadOnlyChannel object.

         @param chan The WReadOnlyChannel object that wraps the
         WChannel object that this WReadOnlyChannel object should
         wrap. 
      */
      WReadOnlyChannel (const WReadOnlyChannel& chan)
      {
         Init (chan);
      }

      WReadOnlyChannel (WReadOnlyChannel&& c):
         m_core_v (std::move (c.m_core_v))
      {}

      WReadOnlyChannel& operator=(WReadOnlyChannel&& c)
      {   
         m_core_v = std::move (c.m_core_v);
         return *this;
      }

      //!{
      /**
         Initializes a channel wrapper.

         @param chan The channel to wrap.
         @param at The current STM transaction.
      */
      void Init (const WChannel<Data_t>& chan)
      {
         Atomically ([&](WAtomic& at){this->Init (chan, at);});
      }
      void Init (const WChannel<Data_t>& chan, WAtomic& at)
      {
         m_core_v.Set (chan.m_core_p, at);
      }
      //!}

      //!{
      /**
         Initializes a channel wrapper.

         @param chan The WReadOnlyChannel that wraps the WChannel
         that this WReadOnlyChannel is to wrap.
         @param at The current STM transaction.
      */
      void Init (const WReadOnlyChannel& chan)
      {
         Atomically ([&](WAtomic& at){this->Init (chan, at);});
      }
      void Init (const WReadOnlyChannel& chan, WAtomic& at)
      {
         Internal::WChannelCore<Data_t>::Ptr chan_p = chan.m_core_v.Get (at).lock ();
         m_core_v.Set (chan_p, at);
      }
      //!}
         
      /**
         Returns true if this object is initialized and the
         channel still exists, false otherwise.
      */
      operator bool () const
      {
         return !m_core_v.GetReadOnly ().expired ();
      }

      //!{
      /**
         Releases the WReadOnlyChannel's hold on it's wrapped
         WChannel.
      */
      void Release ()
      {
         Atomically ([&](WAtomic& at){this->Release (at);});
      }
      void Release (WAtomic& at)
      {
         m_core_v.Set (CoreWPtr (), at);
      }
      //!}

      /**
         Connects a function to the "data written" signal.  This
         is a ThreadSafeSignal::Signal that is emitted whenever
         data is written into the channel.  Note that the usual
         caveats about using ThreadSafeSignal::Signal apply.

         @param func The function to connect to the signal.
         Must be callable as void ().

         @return The connection object for the established
         connection.
            
         @throw WInvalidChannelError if the underlying channel no
         longer exists or the WReadOnlyChannel was never
         initialized.
      */
      template <typename Func_t>
      typename WChannel<Data>::WWriteSignal::Connection
      ConnectToWriteSignal (Func_t func, NO_ATOMIC)
      {
         Internal::WChannelCore<Data_t>::Ptr core_p = m_core_v.GetReadOnly ().lock ();
         if (core_p)
         {
            return core_p->m_writeSignal_p->Connect (func);
         }
         else
         {
            throw WInvalidChannelError ();               
         }
      }
      
   private:
      typedef boost::weak_ptr<Internal::WChannelCore<Data_t>> CoreWPtr;
      WVar<CoreWPtr> m_core_v;
   };

   /**
      A writable reference to a channel.  These should be used
      when a writable reference to a channel is needed by some
      other code.  This reference will be weak, when there are no
      readers left and the original channel object is gone then
      WChannelWriter objects will become invalid and not keep the
      channel's resources around.  This is to prevent writing to
      a channel that has no readers and no way to ever get more
      readers.
   */
   template <typename Data_t>
   class WChannelWriter
   {
      typedef Internal::WChannelCore<Data_t> Core;
      typedef typename Core::Ptr CorePtr;
   public:
      typedef Data_t Data;
         
      /**
         Creates an invalid WChannelWriter object.  Use init or
         operator= to initialize the object.
      */
      WChannelWriter ()
      {}
         
      /**
         Creates a WChannelWriter that writes to the given
         channel.

         @param chan The channel to write to.
      */
      WChannelWriter (const WChannel<Data_t>& chan)
      {
         Init (chan);
      }

      /**
         Initializes a WChannelWriter.

         @param chan The channel to write to.
      */
      void Init (const WChannel<Data_t>& chan)
      {
         m_core_p = chan.m_core_p;
      }

      WChannelWriter (WChannelWriter&& c):
         m_core_p (std::move (c.m_core_p))
      {}

      WChannelWriter& operator=(WChannelWriter&& c)
      {
         m_core_p = std::move (c.m_core_p);
         return *this;
      }

      /**
         Releases the underlying channel and renders the
         WChannelWriter object invalid.
      */
      void Release ()
      {
         m_core_p.reset ();
      }

      /**
         Returns true if the WChannelWriter object is valid,
         false otherwise.
      */
      operator bool () const
      {
         return !m_core_p.expired ();
      }

      /**
         Writes the given data into the channel.

         @param data_p The data to write.

         @return true if the message could be written, false if
         the channel no longer exists.
      */
      bool Write (const Data& data)
      {
         CorePtr core_p = m_core_p.lock ();
         if (core_p)
         {
            Atomically ([&](WAtomic& at){core_p->Write (data, at);});
            return true;
         }
         else
         {
            return false;
         }
      }

      /**
         Writes the given data into the channel. Uses the given
         transaction instead of creating a new one.

         @param data_p The data to write.

         @param at The transaction to use.
            
         @return true if the message could be written, false if
         the channel no longer exists.
      */
      bool Write (const Data& data, WAtomic& at)
      {
         CorePtr core_p = m_core_p.lock ();
         if (core_p)
         {
            core_p->Write (data, at);
            return true;
         }
         else
         {
            return false;
         }
      }
         
   private:
      boost::weak_ptr<Core> m_core_p;
   };

   /**
      The read end of a multi-cast channel. These objects are
      used to read messages that were written to a
      WChannel<Data_t>.  The read operations are protected by STM
      transactions.
       
      Note that the reader will start reading messages from the
      next message that is written to the channel after the
      reader is initialized.  The reader will not see any message
      written to the channel before the reader is initialized.
        
      @param Data_t The type of data sent through the
      channel. This type must be copyable. Normally this should
      be a boost::shared_ptr<const Val_t> where Val_t is the
      actual data type being sent.
   */
   template <typename Data_t>
   class WChannelReader
   {
      typedef typename Internal::WChannelCore<Data_t>::WNode Node;

   public:
      //!The type of data read from the channel.
      typedef Data_t Data;

      /**
         Data type returned by some read operations.  This will be
         empty if the read timed out.
      */
      typedef boost::optional<Data> DataOpt;

      /**
       * Desctructor. This is virtual so that we can do RTTI on WChannelReader objects (this needs
       * to be done for internal reasons).
       */
      virtual ~WChannelReader () 
      {}
      
      /**
         Default constructor.  Do no attempt to use any methods
         other than assignment or initialization methods until
         the WChannelReader object has been initialized.  Failure
         to do this will result in an WInvalidChannelError
         exception.
      */
      WChannelReader ():
         m_data_p (new WData)
      {}

      /**
         Constructor.
            
         @param ch The channel to read messages from.
      */
      WChannelReader (const WChannel<Data>& ch):
         m_data_p (new WData)
      {
         Init (ch);
      }

      /**
         Constructor.
            
         @param ch The channel to read messages from.
            
         @param at The transaction to create the reader within.
      */
      WChannelReader (const WChannel<Data>& ch, WAtomic& at):
         m_data_p (new WData)
      {
         Init (ch, at);
      }

      /**
         Constructor.
          
         @param ch The channel to read messages from.

         @throw WInvalidChannelError if the underlying channel no
         longer exists or the WReadOnlyChannel was never
         initialized.
      */
      WChannelReader (const WReadOnlyChannel<Data>& ch):
         m_data_p (new WData)
      {
         Init (ch);
      }

      /**
         Constructor.
          
         @param ch The channel to read messages from.
           
         @param at The transaction to create the reader within.

         @throw WInvalidChannelError if the underlying channel no
         longer exists or the WReadOnlyChannel was never
         initialized.
      */
      WChannelReader (const WReadOnlyChannel<Data>& ch, WAtomic& at):
         m_data_p (new WData)
      {
         Init (ch, at);
      }

      /**
         Copy constructor.
          
         @param reader The reader to copy.  Note that the reader
         will be copied in its current state so the new reader
         will start receiving messages with the next message
         that the original reader receives.  The new reader will
         not see any of the messages that the original reader
         received before the new reader was created.
      */
      WChannelReader (const WChannelReader& reader):
         m_data_p (new WData)
      {
         Atomically ([&](WAtomic& at){this->Copy (reader, at);});
      }

      /**
         Copy constructor.
          
         @param reader The reader to copy.  Note that the reader
         will be copied in its current state so the new reader
         will start receiving messages with the next message
         that the original reader receives.  The new reader will
         not see any of the messages that the original reader
         received before the new reader was created.
           
         @param at The transaction to create the reader within. 
      */
      WChannelReader (const WChannelReader& reader, WAtomic& at):
         m_data_p (new WData)
      {
         Copy (reader, at);
      }

      WChannelReader (WChannelReader&& c):
         m_data_p (std::move (c.m_data_p))
      {}

      WChannelReader& operator=(WChannelReader&& c)
      {
         m_data_p = std::move (c.m_data_p);
         return *this;
      }
      
      /**
         Assignment operator.
          
         @param reader The reader to copy.  Note that the reader
         will be copied in its current state so the new reader
         will start receiving messages with the next message
         that the original reader receives.  The new reader will
         not see any of the messages that the original reader
         received before the new reader was created.
      */
      WChannelReader& operator= (const WChannelReader& reader)
      {
         Atomically ([&](WAtomic& at){Copy (reader, at);});
         return *this;
      }

      /**
         Copies the given reader.
          
         @param reader The reader to copy.  Note that the reader
         will be copied in its current state so the new reader
         will start receiving messages with the next message
         that the original reader receives.  The new reader will
         not see any of the messages that the original reader
         received before the new reader was created.
           
         @param at The transaction to copy the reader within. 
      */
      void Copy (const WChannelReader& reader, WAtomic& at)
      {
         m_data_p->Release (at);
         auto core_p = reader.m_data_p->m_core_v.Get (at);
         if (core_p)
         {
            m_data_p->m_cur_v.Set (core_p->AddReader (at), at);
         }
         else
         {
            m_data_p->m_cur_v.Set (nullptr, at);
         }
         m_data_p->m_core_v.Set (core_p, at);
      }

      /**
         Initializes the reader.  It is safe to call this on
         readers that are already initialized.
          
         @param ch The channel to read messages from.
      */
      void Init (const WChannel<Data>& ch)
      {
         Atomically ([&](WAtomic& at){InitFromChannel (ch, at);});
      }

      /**
         Initializes the reader.  It is safe to call this on
         readers that are already initialized.
          
         @param ch The channel to read messages from.
           
         @param at The transaction to create the reader within. 
      */
      void Init (const WChannel<Data>& ch, WAtomic& at)
      {
         InitFromChannel (ch, at);
      }

      /**
         Initializes the reader.  It is safe to call this on
         readers that are already initialized.
          
         @param ch The channel to read messages from.

         @throw WInvalidChannelError if the underlying channel no
         longer exists or the WReadOnlyChannel was never
         initialized.
      */
      void Init (const WReadOnlyChannel<Data>& ch)
      {
         Atomically ([&](WAtomic& at){InitFromReadonlyChannel (ch, at);});
      }

      /**
         Initializes the reader.  It is safe to call this on
         readers that are already initialized.
          
         @param ch The channel to read messages from.
           
         @param at The transaction to create the reader within. 
            
         @throw WInvalidChannelError if the underlying channel no
         longer exists or the WReadOnlyChannel was never
         initialized.
      */
      void Init (const WReadOnlyChannel<Data>& ch, WAtomic& at)
      {
         InitFromReadonlyChannel (ch, at);
      }

      /**
         Releases the WChannelReader's reference to the WChannel
         rendering the WChannelReader invalid.
      */
      void Release ()
      {
         Atomically ([&](WAtomic& at){m_data_p->Release (at);});
      }

      /**
         Releases the WChannelReader's reference to the WChannel
         rendering the WChannelReader invalid.
            
         @param at The transaction to use.
      */
      void Release (WAtomic& at)
      {
         m_data_p->Release (at);
      }

      /**
         Returns true if the WChannelReader is valid, false otherwise.
      */
      operator bool () const
      {
         return Atomically ([&](WAtomic& at){return Valid (at);});
      }

      /**
         Returns true if the WChannelReader is valid, false otherwise.
            
         @param at The transaction to check validiity within.
      */
      bool Valid (WAtomic& at) const
      {
         auto cur_p = m_data_p->m_cur_v.Get (at);
         CorePtr core_p = m_data_p->m_core_v.Get (at);
         return (cur_p && core_p);
      }

      /**
         Waits for messages to become available.
          
         @param timeout The amount of milliseconds to wait.  Set
         to STM::UNLIMITED to wait forever.
          
         @return true if messages are now available, false if
         the wait timed out.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      bool Wait (const WTimeArg& timeout = WTimeArg::UNLIMITED ())
      {
         try
         {
            Atomically ([&](WAtomic& at){RetryIfEmpty (at, timeout);});
         }
         catch (WRetryTimeoutException&)
         {
            return false;
         }
         return true;
      }

      /**
         Checks for available messages.  If new messages are not
         available then it calls STM::retry.
          
         @param at The current STM transaction.
          
         @param timeout The timeout to pass to STM::retry.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      void RetryIfEmpty (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ())
      {
         CorePtr core_p = m_data_p->m_core_v.Get (at);
         if (!core_p)
         {
            throw WInvalidChannelError ();
         }
         
         auto cur_p = m_data_p->m_cur_v.Get (at);
         if (!cur_p)
         {
            throw WInvalidChannelError ();
         }

         auto next_p = cur_p->m_next_v.Get (at);
         if (!next_p)
         {
            Retry (at, timeout);
         }
      }

      /**
         Returns the first available message without removing
         it from the channel.
          
         @return A DataOpt object the will be initialized if a
         message was available and uninitialized if there was
         not.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      DataOpt Peek ()
      {
         return Atomically ([&](WAtomic& at){return Peek (at);});
      }

      /**
         Returns the first available message without removing
         it from the channel.
          
         @param at The current STM transaction.
          
         @return A DataOpt object the will be initialized if a
         message was available and uninitialized if there was
         not.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      DataOpt Peek (WAtomic& at)
      {
         CorePtr core_p = m_data_p->m_core_v.Get (at);
         if (!core_p)
         {
            throw WInvalidChannelError ();
         }

         auto cur_p = m_data_p->m_cur_v.Get (at);
         if (!cur_p)
         {
            throw WInvalidChannelError ();
         }
         auto next_p = cur_p->m_next_v.Get (at);
         if (next_p)
         {
            DataOpt data_o = DataOpt (next_p->m_data);
            return data_o;
         }
         else
         {
            return DataOpt ();
         }
      }

   private:
      class WDeadNodeQueue
      {
         std::deque<boost::shared_ptr<Node>> m_nodes;

      public:
         ~WDeadNodeQueue ()
         {
            //need to clear nodes from front to back in order to avoid the stack overflow due to how
            //the nodes are linked
            while (!m_nodes.empty ())
            {
               m_nodes.pop_front ();
            }
         }

         void Push (const boost::shared_ptr<Node>& node_p)
         {
            m_nodes.push_back (node_p);
         }
      };

      WTransactionLocalValue<boost::shared_ptr<WDeadNodeQueue>> m_deadNodes;
      
      void SaveDeadNode (const boost::shared_ptr<Node>& node_p, WAtomic& at)
      {
         //In order to avoid stack overflows when a lot of nodes are read in a single transaction we
         //need to capture the nodes in a deque that will be emptied iteratively when the transaction
         //is done.
         auto deadNodes_p = boost::shared_ptr<WDeadNodeQueue>();
         if (auto deadNodes_pp = m_deadNodes.Get (at))
         {
            deadNodes_p = *deadNodes_pp;
         }
         else
         {
            deadNodes_p = boost::make_shared<WDeadNodeQueue>();
            m_deadNodes.Set (deadNodes_p, at);
            //We need the following "After action" in order to force the dead nodes to stick around
            //long enough in the commit cycle to avoid the stack overflow
            at.After ([deadNodes_p]() {});
         }

         deadNodes_p->Push (node_p);         
      }
      
   public:

      /**
         Returns the first available message. If there are no
         messages then it calls STM::Retry with the given
         timeout.
         
         @param at The current STM transaction.
         
         @param timeout The timeout passed to STM::Retry if there are
         no messages available.
         
         @return A DataOpt object the will be initialized if a message was available, if no message
         is available the transaction is retried and there is no return value.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      DataOpt ReadRetry (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ())
      {
         CorePtr core_p = m_data_p->m_core_v.Get (at);
         if (!core_p)
         {
            throw WInvalidChannelError ();
         }

         auto cur_p = m_data_p->m_cur_v.Get (at);
         if (!cur_p)
         {
            throw WInvalidChannelError ();
         }

         SaveDeadNode (cur_p, at);
         
         if (cur_p->m_initial)
         {
            m_data_p->m_cur_v.Set (cur_p->m_next_v.Get (at), at);
            return cur_p->m_data;
         }
            
         auto next_p = cur_p->m_next_v.Get (at);
         if (next_p)
         {
            DataOpt data_o = DataOpt (next_p->m_data);
            m_data_p->m_cur_v.Set (next_p, at);
            return data_o;
         }
         else
         {
            Retry (at, timeout);
            //Retry throws an exception, this is just here to satisfy
            //a compiler warning
            return boost::none;
         }
      }
      
      /**
         Returns the first available message.
         
         @param timeout How long to wait for a message
         (milliseconds).
         
         @return A DataOpt object the will be initialized if a
         message was available and uninitialized if there was
         no message available and one did not become available within
         the given timeout.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      DataOpt Read (const WTimeArg& timeout = WTimeArg::UNLIMITED ())
      {
         try
         {
            return Atomically ([&](WAtomic& at){return ReadRetry (at, timeout);});
         }
         catch (WRetryTimeoutException&)
         {
            return DataOpt ();
         }
      }

      /**
       * Returns the first available message.
       *
       * @param at The current STM transaction.
       *
       * @return An initialized DataOpt object if a message is
       * available. If no message is available the DataOpt object will
       * be uninitialized.
       *
       * @throw WInvalidChannelError if the WChannelReader is not
       * initialized.
       */
      DataOpt ReadAtomic (WAtomic& at)
      {
         CorePtr core_p = m_data_p->m_core_v.Get (at);
         if (!core_p)
         {
            throw WInvalidChannelError ();
         }
         
         auto cur_p = m_data_p->m_cur_v.Get (at);
         if (!cur_p)
         {
            throw WInvalidChannelError ();
         }

         SaveDeadNode (cur_p, at);
         
         if (cur_p->m_initial)
         {
            m_data_p->m_cur_v.Set (cur_p->m_next_v.Get (at), at);
            return cur_p->m_data;
         }
            
         auto next_p = cur_p->m_next_v.Get (at);
         if (next_p)
         {
            DataOpt data_o = DataOpt (next_p->m_data);
            m_data_p->m_cur_v.Set (next_p, at);
            return data_o;
         }
         else
         {
            return DataOpt ();
         }         
      }

      /**
         Reads all available messages from the channel.
          
         @return A vector containing the messages.  The vector
         will be empty if there were no messages available.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      std::vector<Data> ReadAll ()
      {
         const unsigned int MAX_CHANNEL_READ_ALL_CONFLICTS = 5;

         return Atomically ([&](WAtomic& at){return ReadAll (at);},
                            WMaxConflicts (MAX_CHANNEL_READ_ALL_CONFLICTS) & WConRes (WConflictResolution::RUN_LOCKED));
      }
   
      /**
         Reads all available messages from the channel.
          
         @param at The current STM transaction.
          
         @return A vector containing the messages.  The vector
         will be empty if there were no messages available.

         @throw WInvalidChannelError if the WChannelReader is not initialized.
      */
      std::vector<Data> ReadAll (WAtomic& at)
      {
         std::vector<Data> values;
         DataOpt val_o = ReadAtomic (at);
         while (val_o)
         {
            values.push_back (val_o.get ());
            val_o = ReadAtomic (at);
         }
         return values;
      }
      
   private:
      typedef typename Internal::WChannelCore<Data>::Ptr CorePtr;
      
      struct WData
      {
         WVar<boost::shared_ptr<Node>> m_cur_v;
         
         //We keep a reference to the channel's core so that it
         //sticks around as long as we do.  That way
         //WReadOnlyChannel's and WChannelWriter's will still be
         //valid as long as a channel has readers.  
         WVar<CorePtr> m_core_v;

         void Release (WAtomic& at)
         {
            CorePtr core_p = m_core_v.Get (at);
            if (!core_p)
            {
               return;
            }
            core_p->RemoveReader (at);
            
            auto cur_p = m_cur_v.Get (at);
            if (!cur_p)
            {
               return;
            }

            //We can't just release the current node pointer: if enough messages have been written
            //to the channel since this reader was last read from then we can end up with a stack
            //overflow when the ndoes are walked and reclaimed. To avoid this we copy all the nodes
            //into another data structure so that they can't go to zero ref count during the
            //transaction. After the transaction ends we go through the container of node pointers
            //releasing each on its own in a loop so that the stack doesn't overflow.
            m_cur_v.Set (nullptr, at);
            auto release_p = boost::make_shared<std::deque<boost::shared_ptr<Node>>>();
            while (cur_p)
            {
               release_p->push_back (cur_p);
               cur_p = cur_p->m_next_v.Get (at);
            }
            at.After ([release_p]()
                      {
                         BOOST_FOREACH (auto& cur_p, *release_p)
                         {
                            cur_p = nullptr;
                         }
                      });
            m_core_v.Set (CorePtr (), at);
         }

         ~WData ()
         {
            Atomically ([&](WAtomic& at){this->Release (at);});
         }
      };
      std::unique_ptr<WData> m_data_p;

      void InitFromChannel (const WChannel<Data>& ch, WAtomic& at)
      {
         m_data_p->Release (at);
         m_data_p->m_cur_v.Set (ch.m_core_p->AddReader (at), at);
         m_data_p->m_core_v.Set (ch.m_core_p, at);
      }

      void InitFromReadonlyChannel (const WReadOnlyChannel<Data>& ch, WAtomic& at)
      {
         Internal::WChannelCore<Data>::Ptr core_p = ch.m_core_v.Get (at).lock ();
         if (core_p)
         {
            m_data_p->Release (at);
            m_data_p->m_cur_v.Set (core_p->AddReader (at), at);
            m_data_p->m_core_v.Set (core_p, at);
         }
         else
         {
            throw WInvalidChannelError ();
         }
      }
   };

   //!{
   /**
    * Creates a reader for the given channel.
    */
   template <typename Data_t>
   WChannelReader<Data_t> MakeReader (const WChannel<Data_t>& ch)
   {
      return WChannelReader<Data_t>(ch);
   }
   
   template <typename Data_t>
   WChannelReader<Data_t> MakeReader (const WChannel<Data_t>& ch, WAtomic& at)
   {
      return WChannelReader<Data_t>(ch, at);
   }
   
   template <typename Data_t>
   WChannelReader<Data_t> MakeReader (const WReadOnlyChannel<Data_t>& ch)
   {
      return WChannelReader<Data_t>(ch);
   }
   
   template <typename Data_t>
   WChannelReader<Data_t> MakeReader (const WReadOnlyChannel<Data_t>& ch, WAtomic& at)
   {
      return WChannelReader<Data_t>(ch, at);
   }
   //!}
   
}}}

#ifdef WIN32
#pragma warning (pop)
#endif
