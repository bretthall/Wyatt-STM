/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#pragma once

#include "stm.h"
#include "exports.h"

namespace WSTM
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
   class WSTM_CLASSAPI WExceptionCapture
   {
   public:
      /**
         Creats an empty wrapper.
      */
      WExceptionCapture ();

      //!{
      /**
       * Creates a wrapper that contains the given exception.
       */
      template <typename Capture_t>
      WExceptionCapture (const Capture_t& exc)
      {
         Capture (exc);
      }
      template <typename Capture_t>
      WExceptionCapture (const Capture_t& exc, WAtomic& at)
      {
         Capture (exc, at);
      }
      //!}
      
      //!{
      /**
         Copies/moves the error from the given wrapper into this one.
      */
      WExceptionCapture (const WExceptionCapture& exc);
      WExceptionCapture& operator= (const WExceptionCapture& exc);

      WExceptionCapture (WExceptionCapture&& exc);
      WExceptionCapture& operator=(WExceptionCapture&& exc);
      //!}
      
      //!{
      /**
         Captures the given exception. If the object to capture is
         another WExceptionCapture or WExceptionCapturethen the
         object captured by exc will be captured instead. 

         @param exc The exception to capture, must be copyable.
         @param at The STM transaction to use.
      */
      void Capture (const WExceptionCapture& exc);
      void Capture (const WExceptionCapture& exc, WAtomic& at);
      template <typename Capture_t>
      void Capture (const Capture_t& exc)
      {
         m_thrower_v.Set ([exc](){throw exc;});
      }
      template <typename Capture_t>
      void Capture (const Capture_t& exc, WAtomic& at)
      {
         m_thrower_v.Set ([exc](){throw exc;}, at);
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
      typedef std::function<void ()> Thrower;
      WVar<Thrower> m_thrower_v;
   };


}

