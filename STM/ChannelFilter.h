/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

#pragma once

#include "Channel.h"

#include <boost/function.hpp>

namespace bss { namespace thread { namespace STM
{

   /**
    * Class that filters messages in a WChannel. This filtering is done in the thread that
    * WChannel::Write is being called in. The filtered messages are copied into another
    * WChannel. Note that the filtering will persist after the WChannelFilter object is gone until
    * the channel that filtered messages are copied to is gone. If you want the filtering stop
    * before then you must explicitly call Disconnect.
    *
    * @param Data_t The type of message sent through the channel.
    */
   template <typename Data_t>
   class WChannelFilter
   {
   public:
      DEFINE_POINTERS (WChannelFilter);
      
      /**
       * The type of function used to filter the messages, each
       * message is passed to the filter function and is copied to the
       * filtered channel if this function returns true.
       */
      typedef boost::function<bool (const Data_t&)> FilterFunc;

      /**
       * Creates an uninitialized channel filter.
       */
      WChannelFilter ()
      {}
      
      /**
       * Creates an initialized filter.
       *
       * @param chanIn The channel to be filtered.
       * @param writer Messages that pass the filter are written to
       * this.
       * @param filter The filter function.
       */
      WChannelFilter (WReadOnlyChannel<Data_t> chanIn,
                      WChannelWriter<Data_t> writer,
                      FilterFunc filter)
      {
         Reset (std::move (chanIn), std::move (writer), filter);
      }

      /**
       * Resets the filter to a new channel and filter function.
       *
       * @param chanIn The channel to be filtered.
       * @param writer Messages that pass the filter are written to
       * this.
       * @param filter The filter function.
       */
      void Reset (WReadOnlyChannel<Data_t> chanIn,
                  WChannelWriter<Data_t> writer,
                  FilterFunc filter)
      {
         Disconnect ();
         m_conn_p = boost::make_shared<bss::thread::ThreadSafeSignal::WConnection>();
         *m_conn_p = chanIn.ConnectToWriteSignal (WMsgHandler (MakeReader (chanIn), std::move (writer), filter, m_conn_p));
      }

      /**
       * Disconnects the filter from the channel.
       */
      void Disconnect ()
      {
         if (m_conn_p)
         {
            m_conn_p->Disconnect ();
            m_conn_p = nullptr;
         }
      }
      
   private:

      boost::shared_ptr<bss::thread::ThreadSafeSignal::WConnection> m_conn_p;

      struct WMsgHandler
      {
         WChannelReader<Data_t> m_reader;
         WChannelWriter<Data_t> m_writer;
         FilterFunc m_filter;
         boost::shared_ptr<bss::thread::ThreadSafeSignal::WConnection> m_conn_p;

         WMsgHandler (WChannelReader<Data_t> chanIn,
                      WChannelWriter<Data_t> writer,
                      FilterFunc filter,
                      boost::shared_ptr<bss::thread::ThreadSafeSignal::WConnection> conn_p):
            m_reader (std::move (chanIn)),
            m_writer (std::move (writer)),
            m_filter (filter),
            m_conn_p (std::move (conn_p))
         {} 

         void operator ()()
         {
            if (!m_writer)
            {
               //The other end is gone so disconnect
               if (m_conn_p)
               {
                  m_conn_p->Disconnect ();
                  m_conn_p = nullptr;
               }               
               return;
            }

            WChannelReader<Data_t>::DataOpt msg_o =
               m_reader.Read (boost::posix_time::milliseconds (0));
            if (msg_o && m_filter (*msg_o))
            {
               const Data_t& msg = *msg_o;
               m_writer.Write (msg);
            }
         }

      };

   };

   /**
    * Creates a filter on the given channel.
    *
    * @param channel The channel to filter.
    * @param filter The filter to apply.
    *
    * @return A WChannelReader that will receive the filtered messages.
    */
   template <typename Data_t>
   WChannelReader<Data_t> FilterChannel (const WReadOnlyChannel<Data_t>& channel, typename WChannelFilter<Data_t>::FilterFunc filter)
   {
      auto filtered = WChannel<Data_t>();
      auto filterer = WChannelFilter<Data_t>(channel, filtered, filter);
      return filtered;
   }
   
}}}
