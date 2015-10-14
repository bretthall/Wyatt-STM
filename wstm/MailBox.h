/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "BSS/wtcbss.h"

#include "stm.h"

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/optional.hpp>
#include <boost/call_traits.hpp>
#include <boost/bind.hpp>

namespace bss { namespace thread { namespace STM
{

   /**
    * Exception thrown by WMailBox::BindReadToThread() and
    * WMailBox::BindWriteToThread if the WMailBox object is already
    * bound to a thread.
    */
   struct BSS_CLASSAPI WMailBoxAlreadyBoundError
   {};

   /**
    * Exception thrown by the read and write methods of WMailBox if
    * the WMailBox object has been bound to a thread and the methods
    * are not called from the bound thread.
    */
   struct BSS_CLASSAPI WMailBoxBadThreadError
   {
      WMailBoxBadThreadError (const char* method);

      /**
       * The name of the method that threw the exception.
       */
      const std::string m_method;

   private:
      WMailBoxBadThreadError& operator= (const WMailBoxBadThreadError&) { return *this; }
   };

   /**
    * A variable that can hold one value at a time for exchanging
    * messages between threads. Once a value is written no more values
    * can be written until the value is read.
    *
    * @param Type_t The type stored in the mailbox. This type must be
    * storable in boost::optional. Currently in-place factories for
    * value initialization are not supported but could be added if the
    * need arises.
    */
   template <typename Type_t>
   class WMailBox
   {
   public:      
      /**
       * The type stored in the mail box.
       */
      typedef Type_t Type;

      /**
       * The type returned by read functions.
       */
      typedef boost::optional<Type> DataOpt;
      
      /**
       * The type passed to Write.
       */
      typedef typename boost::call_traits<Type>::param_type ParamType;
   
      //!{
      /**
       * Constructor.
       *
       * @param val The initial value for the mailbox. If not passed in
       * the mailbox will start off empty.
       */
      WMailBox ();
      WMailBox (ParamType val);
      //!}

      /**
       * Binds read operations to the calling thread. After this call
       * any calls that read the value can only be done on the bound
       * thread; trying to read from any other thread will cause
       * WMailBoxBadThreadError to be thrown. This method can only be
       * called once for a given mailbox object.
       *
       * @throw WMailBoxAlreadyBoundError if this method has already been
       * called for this object.
       */
      void BindReadToThread ();

      //!{
      /**
       * Returns true if there is a message to read. Note that only
       * the atomic version can be trusted if the mailbox allows
       * multiple readers.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      bool CanRead () const;
      bool CanRead (WAtomic& at) const;
      //!}

      /**
       * Waits for the mailbox to be readable. Note that if the
       * mailbox allows multiple readers then this method will not be
       * reliable. When there are multiple readers you need to use
       * Read to be sure that you will get a message.
       *
       * @param timeout The number of milliseconds to wait for a
       * message.
       *
       * @return true if there is a message to read, false if the
       * timeout expired.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      bool WaitForReadable (const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;

      /**
       * Checks the mailbox for a message, if there is no message
       * available then STM::Retry is called.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to STM::Retry.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      void RetryIfNotReadable (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;
      
      /**
       * Reads and clears the value from the mailbox. If there is no
       * value in the mailbox this method will block for the given
       * timeout.
       *
       * @param timeout The amount of time to wait for a value. If this
       * is zero then this method will return immediately whether there
       * is a value or not. If this is UNLIMITED (the default) then this
       * method will block until a value is available.
       * 
       * @return A boost::optional initialized to the mailbox' value if
       * there is one or uninitialized if no value was available within the
       * time limit.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      DataOpt Read (const WTimeArg& timeout = WTimeArg::UNLIMITED ());

      /**
       * Reads and clears the value from the mailbox. 
       *
       * @param at The current STM transaction.
       * 
       * @return A DataOpt initialized to the mailbox' value if
       * there is one, otherwise an uninitialized DataOpt is returned.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      DataOpt ReadAtomic (WAtomic& at);

      /**
       * Reads and clears the value from the mailbox. Calls STM::Retry
       * if no value is available.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to STM::Retry.
       * 
       * @return The mailbox's current value.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      Type ReadRetry (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ());

      //!{
      /**
       * Reads but does not clear the value from the mailbox. It is
       * generally only a good idea to use this method if
       * BindReadToThread has been called on the mailbox object. If
       * there is no value in the mailbox this method will block for
       * the given timeout.
       *
       * @return A boost::optional initialized to the mailbox' value if
       * there is one or uninitialized if no value was available.
       *
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * read thread and this method is called from a thread other
       * than the bound one.
       */
      DataOpt Peek () const;
      DataOpt Peek (WAtomic& at) const;
      //!}

      /**
       * Binds write operations to the calling thread. After this call
       * any calls to Write can only be done on the bound thread. Calling
       * Write from any other thread will cause WMailBoxBadThreadError to
       * be thrown. This method can only be called once for a given
       * mailbox object.
       *
       * @throw WMailBoxAlreadyBoundError if this method has already been
       * called for this object.
       */
      void BindWriteToThread ();

      //!{
      /**
       * Returns true if a message can be written. The non-atomic
       * version is unreliable if the mailbox allows multiple
       * writers.
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      bool CanWrite () const;
      bool CanWrite (WAtomic& at) const;
      //!}
      
      /**
       * Blocks until a message is available or the given timeout
       * expires. This method is only reliable if the mailbox only
       * allows one writer thread; if multiple writer threads are
       * allowed then Write must be used in order to ensure that
       * messages can actually be sent. 
       *
       * @param timeout The number of milliseconds to wait for a
       * message.
       *
       * @return true if a message is available, false if the timeout
       * expires.
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      bool WaitForWriteable (const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;

      /**
       * Calls STM::Retry if the mailbox is not currently writable.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to STM::Retry. 
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      void RetryIfNotWritable (WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ()) const;

      /**
       * Writes a value into the mailbox blocking for the given
       * timeout if there is already a message in the mailbox.
       *
       * @param value The value to set.
       * 
       * @param timeout The amount of time to wait to write.
       * 
       * @return true if the write could be done, false if the write
       * timed out.
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      bool Write (ParamType value, const WTimeArg& timeout = WTimeArg::UNLIMITED ());

      /**
       * Writes a value into the mailbox, calls STM::Retry if the
       * mailbox is already full.
       *
       * @param value The value to put into the mailbox.
       *
       * @param at The current STM transaction.
       *
       * @param timeout The timeout to pass to STM::Retry.
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      void WriteRetry (ParamType value, WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ());

      /**
       * Writes a value into the mailbox if it isn't already full.
       *
       * @param value The value to put into the mailbox.
       *
       * @param at The current STM transaction.
       *
       * @return true if the message could be written, false
       * otherwise.
       * 
       * @throw WMailBoxBadThreadError if the mailbox is bound to a
       * write thread and this method is called from a thread other
       * than the bound one.
       */
      bool WriteAtomic (ParamType value, WAtomic& at);
   
   private:
      boost::optional<boost::thread::id> m_boundReadThread;
      boost::optional<boost::thread::id> m_boundWriteThread;

      WVar<DataOpt> m_value_v;

      bool CanReadImpl (WAtomic& at) const;
      DataOpt PeekImpl (WAtomic& at) const;
      bool CanWriteImpl (WAtomic& at) const;
   };

   template <typename Type_t>
      WMailBox<Type_t>::WMailBox ()
   {}

   template <typename Type_t>
      WMailBox<Type_t>::WMailBox (ParamType val):
      m_value_v (DataOpt(val))
   {}

   template <typename Type_t>
   void WMailBox<Type_t>::BindReadToThread ()
   {
      if (m_boundReadThread)
      {
         throw WMailBoxAlreadyBoundError ();
      }

      m_boundReadThread = boost::this_thread::get_id ();
   }

   template <typename Type_t>
   bool WMailBox<Type_t>::CanRead () const
   {
      return Atomically (boost::bind (&WMailBox::CanReadImpl, this, _1));
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::CanRead (WAtomic& at) const
   {
      return CanReadImpl (at);
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::CanReadImpl (WAtomic& at) const
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("CanReadAtomic");
      }

      return m_value_v.Get (at);
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::WaitForReadable (const WTimeArg& timeout) const
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("WaitForReadable");
      }

      try
      {
         Atomically (boost::bind (&WMailBox::RetryIfNotReadable, this, _1, boost::cref (timeout)));
         return true;
      }
      catch(WRetryTimeoutException&)
      {
         return false;
      }
   }
   
   template <typename Type_t>
   void WMailBox<Type_t>::RetryIfNotReadable (WAtomic& at, const WTimeArg& timeout) const
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("RetryIfNotReadable");
      }

      if (!m_value_v.Get (at))
      {
         Retry (at, timeout);
      }
   }
   
   template <typename Type_t>
   boost::optional<Type_t> WMailBox<Type_t>::Read (const WTimeArg& timeout)
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("Read");
      }

      try
      {
         return Atomically (boost::bind (&WMailBox::ReadRetry, this, _1, boost::cref (timeout)));
      }
      catch(WRetryTimeoutException&)
      {
         return boost::none;
      }
   }

   template <typename Type_t>
   boost::optional<Type_t> WMailBox<Type_t>::ReadAtomic (WAtomic& at)
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("Read");
      }

      DataOpt val = m_value_v.Get (at);
      if (val)
      {
         m_value_v.Set (boost::none, at);
      }
      return val;
   }
   
   template <typename Type_t>
   Type_t WMailBox<Type_t>::ReadRetry (WAtomic& at, const WTimeArg& timeout)
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("Read");
      }

      DataOpt val = m_value_v.Get (at);
      if(!val)
      {
         Retry (at, timeout);
      }
      m_value_v.Set (boost::none);
      return val.get ();
   }

   template <typename Type_t>
   boost::optional<Type_t>  WMailBox<Type_t>::Peek () const
   {
      return Atomically (boost::bind (&WMailBox::Peek, this, _1));
   }

   template <typename Type_t>
   boost::optional<Type_t>  WMailBox<Type_t>::Peek (WAtomic& at) const
   {
      return PeekImpl (at);
   }

   template <typename Type_t>
   boost::optional<Type_t>  WMailBox<Type_t>::PeekImpl (WAtomic& at) const
   {
      if (m_boundReadThread && boost::this_thread::get_id () != m_boundReadThread)
      {
         throw WMailBoxBadThreadError ("Peek");
      }

      return m_value_v.Get (at);
   }

   template <typename Type_t>
   void WMailBox<Type_t>::BindWriteToThread ()
   {
      if (m_boundWriteThread)
      {
         throw WMailBoxAlreadyBoundError ();
      }

      m_boundWriteThread = boost::this_thread::get_id ();
   }

   template <typename Type_t>
   bool WMailBox<Type_t>::CanWrite () const
   {
      return Atomically (boost::bind (&WMailBox::CanWriteImpl, this, _1));
   }

   template <typename Type_t>
   bool WMailBox<Type_t>::CanWrite (WAtomic& at) const
   {
      return CanWriteImpl (at);
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::CanWriteImpl (WAtomic& at) const
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("CanWriteAtomic");
      }

      return !m_value_v.Get (at);
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::WaitForWriteable (const WTimeArg& timeout) const
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("WaitForWriteable");
      }

      try
      {
         Atomically (boost::bind (&WMailBox::RetryIfNotWritable, this, _1, boost::cref (timeout)));
         return true;
      }
      catch(WRetryTimeoutException&)
      {
         return false;
      }
   }
   
   template <typename Type_t>
   void WMailBox<Type_t>::RetryIfNotWritable (WAtomic& at, const WTimeArg& timeout) const
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("RetryIfNotWritable");
      }

      if (m_value_v.Get (at))
      {
         Retry (at, timeout);
      }
   }

   template <typename Type_t>
   bool WMailBox<Type_t>::Write (ParamType value, const WTimeArg& timeout)
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("Write");
      }

      try
      {
         Atomically (boost::bind (&WMailBox::WriteRetry,
                                  this, boost::cref (value), _1, boost::cref (timeout)));
         return true;
      }
      catch (WRetryTimeoutException&)
      {
         return false;
      }
   }

   template <typename Type_t>
   void WMailBox<Type_t>::WriteRetry (ParamType value, WAtomic& at, const WTimeArg& timeout)
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("WriteRetry");
      }

      if (!m_value_v.Get (at))
      {
         m_value_v.Set (value, at);
      }
      else
      {
         Retry (at, timeout);
      }
   }
   
   template <typename Type_t>
   bool WMailBox<Type_t>::WriteAtomic (ParamType value, WAtomic& at)
   {
      if (m_boundWriteThread && boost::this_thread::get_id () != m_boundWriteThread)
      {
         throw WMailBoxBadThreadError ("WriteAtomic");
      }

      if (!m_value_v.Get (at))
      {
         m_value_v.Set (value, at);
         return true;
      }
      else
      {
         return false;
      }      
   }
   

}}}

