/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2010. All rights reserved.
****************************************************************************/

#pragma once

#include "stm.h"
#include "DeferredResult.h"
#include "Channel.h"

#include "BSS/Common/Pointers.h"

#include <list>
#include <vector>

#ifdef WIN32
#ifdef _DEBUG
#define dw_new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#else
#define dw_new new
#endif
#else
#define dw_new new
#endif

namespace bss
{
   namespace thread
   {
      namespace STM
      {
         /**
          * Class that is used to wait on WDeferredResult object to be
          set to the done state. Any number of WDeferredResult objects
          can be added to the waiter and when they finish the function
          that is registered is called.
          */
         class BSS_CLASSAPI WDeferredWaiter
         {
         public:
            DEFINE_POINTERS (WDeferredWaiter);
            
            WDeferredWaiter ();
            ~WDeferredWaiter ();

            /**
             * Objects of this type are returned by Add. They can be
             * used to cancel waiting on the WDeferredResult.
             */
            struct BSS_CLASSAPI Tag
            {
               DEFINE_POINTERS (Tag);

               //!{
               /**
                * Cancels the wait.
                */
               virtual void Cancel () = 0;
               virtual void Cancel (WAtomic& at) = 0;
               //!}
               
               virtual ~Tag ();
            };

            //!{
            /**
             * Registers the given function to be called when the
             * given WDeferredResult finishes.
             *
             * @param deferred The WDeferredResult object to wait for.
             * @param handler The function to call when the deferred
             * result object is done. This function must be callable
             * as void (WDeferredResult<Result_t>).
             * @param at The current STM transaction.
             * 
             * @return A Tag object that can be used to cancel the
             * waiting.
             */
            template <typename Result_t, typename Func_t>
            Tag::Ptr Add (const WDeferredResult<Result_t>& deferred, Func_t handler)
            {
               WWait::Ptr wait_p (dw_new WWaitImpl<Result_t, Func_t> (deferred, handler));
               m_newWaits.Write (wait_p);
               return wait_p;
            }
            
            template <typename Result_t, typename Func_t>
            Tag::Ptr Add (const WDeferredResult<Result_t>& deferred,
                          Func_t handler,
                          WAtomic& at)
            {
               WWait::Ptr wait_p (dw_new WWaitImpl<Result_t, Func_t> (deferred, handler));
               m_newWaits.Write (wait_p, at);
               return wait_p.get ();
            }
            //!}
            
         private:
            //NO COPYING! Causes problems with ownership of the
            //background thread
            WDeferredWaiter (const WDeferredWaiter&);
            WDeferredWaiter& operator=(const WDeferredWaiter&);
            
            struct BSS_CLASSAPI WWait : public Tag
            {
               DEFINE_POINTERS (WWait);

               WWait ();
               
               virtual bool Check (WAtomic& at) = 0;
               virtual void Call () = 0;

               virtual void Cancel ();
               virtual void Cancel (WAtomic& at);

               bool m_done;
               WVar<bool> m_cancelled_v;
               bool m_cancelled;
            };
            
            template <typename Result_t, typename Func_t>
            struct WWaitImpl : public WWait
            {
               WWaitImpl (const WDeferredResult<Result_t>& deferred, Func_t handler):
                  m_deferred (deferred), m_handler (handler)
               {
                  m_done = false;
               }

               WDeferredResult<Result_t> m_deferred;
               Func_t m_handler;

               virtual bool Check (WAtomic& at)
               {
                  if (m_deferred.IsDone (at))
                  {
                     m_done = true;
                  }
                  if (m_cancelled_v.Get (at))
                  {
                     m_done = true;
                     m_cancelled = true;
                  }
                  return m_done;
               }
               
               virtual void Call ()
               {
                  m_handler (m_deferred);
               }
            };

            static void DoDeferredWaiting (WChannelReader<WWait::Ptr>& newWaits,
                                           const WVar<bool>::Ptr& shutdown_pv);
            static bool DoDeferredWaitingAt (std::list<WWait::Ptr>& waits,
                                             std::vector<WWait::Ptr>& newWaitObjs,
                                             WChannelReader<WWait::Ptr>& newWaits,
                                             const WVar<bool>::Ptr& shutdown_pv,
                                             bss::thread::STM::WAtomic& at);
            static void CleanupDeferreds (std::list<WWait::Ptr>& waits,
                                          std::vector<WWait::Ptr>& newWaitObjs);
            static void AddDeferreds (std::list<WWait::Ptr>& waits,
                                      std::vector<WWait::Ptr>& newWaitObjs);
               
            WChannelWriter<WWait::Ptr> m_newWaits;
            WVar<bool>::Ptr m_shutdown_pv;
         };
      }
   }
}
