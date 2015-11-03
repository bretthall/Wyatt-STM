/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "STM.h"
#include "BSS/wtcbss.h"
#include "BSS/Common/ExceptionCapture.h"

#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>
#include <boost/function.hpp>

namespace bss { namespace thread { namespace STM
{
   /**
      Class that wraps exceptions so that they can be thrown later.
      An exception object is passed to Capture () and a copy is made.
      Then at some later point ThrowCaptured () can be called to cause
      that exception to be thrown.  Note that wrapped exceptions must
      be copyable. This version is integrated with the STM system. If
      this integration is not needed then WExceptionCapture should
      probably be used instead.
   */
   class BSS_CLASSAPI WExceptionCaptureAtomic : public WExceptionCaptureBase
   {
   public:
      friend class ::bss::WExceptionCapture;
      
      /**
         Creats an empty wrapper.
      */
      WExceptionCaptureAtomic ();
        
      /**
         Copies the error from the given wrapper into this one.
      */
      WExceptionCaptureAtomic (const WExceptionCaptureAtomic& exc);

      /**
         Copies the error from the given wrapper into this one.
      */
      WExceptionCaptureAtomic (const ::bss::WExceptionCapture& exc);

      template <typename Capture_t>
      WExceptionCaptureAtomic (const Capture_t& exc,
                               typename boost::disable_if<
                               boost::is_base_of <WExceptionCaptureBase, Capture_t> >::type*
                               /*dummy*/ = 0)
      {
         Capture (exc);
      }
      
      //!{
      /**
         Captures the given exception. If the object to capture is
         another WExceptionCaptureAtomic or WExceptionCapturethen the
         object captured by exc will be captured instead. 

         @param exc The exception to capture, must be copyable.
         @param at The STM transaction to use.
      */
      void Capture (const WExceptionCaptureAtomic& exc);
      void Capture (const WExceptionCaptureAtomic& exc, WAtomic& at);
      void Capture (const WExceptionCapture& exc);
      void Capture (const WExceptionCapture& exc, WAtomic& at);
      template <typename Capture_t>
      void Capture (const Capture_t& exc,
                    typename boost::disable_if<
                    boost::is_base_of <WExceptionCaptureBase, Capture_t> >::type* /*dummy*/ = 0)
      {
         m_thrower_v.Set (WThrower<Capture_t> (exc));
      }
      template <typename Capture_t>
      void Capture (const Capture_t& exc, WAtomic& at,
                    typename boost::disable_if<
                    boost::is_base_of <WExceptionCaptureBase, Capture_t> >::type* /*dummy*/ = 0)
      {
         m_thrower_v.Set (WThrower<Capture_t> (exc), at);
      }
      //!}

      //!{
      /**
       * Clears out any captured exceptions.
       */
      void Reset ();
      void Reset (WAtomic& at);
      //!}
      
      //!{
      /**
         Throws the wrapped exception.  Does nothing if there has
         been no exception captured.
         
         @param at The STM transaction to use.
      */
      void ThrowCaptured () const;
      void ThrowCaptured (WAtomic& at) const;
      //!}

      //!{
      /**
         Returns true if an exception has been captured.
         
         @param at The STM transaction to use.
      */
      operator bool () const;
      bool HasCaptured (WAtomic& at) const;
      //!}
      
   private:
      void CaptureAt (const WExceptionCaptureAtomic& exc, WAtomic& at);

      template <typename Capture_t>
      struct WThrower
      {
         Capture_t m_exc;
         WThrower (const Capture_t& exc) : m_exc (exc) {}
         void operator () ()
         {
            throw m_exc;
         }
      };
      
      typedef boost::function<void ()> Thrower;
      WVar<Thrower> m_thrower_v;
   };


}}}

