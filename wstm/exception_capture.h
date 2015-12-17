// Copyright (c) 2015, Wyatt Technology Corporation
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:

// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "stm.h"
#include "exports.h"

/**
 * @file exception_capture.h
 * A system for capturing exceptions so that they can be thrown in other threads. 
 */

namespace WSTM
{
   /**
    * @defgroup ExcCap Exception Capture
    *
    * A system for capturing exceptions so that they can be thrown in other threads. 
    */
   ///@{

   /**
    * Class that wraps exceptions so that they can be thrown later. An exception object is passed
    * to Capture and a copy is made. Then at some later point ThrowCaptured can be called to
    * cause that exception to be thrown. Note that wrapped exceptions must be copyable.
    */
   class WSTM_CLASSAPI WExceptionCapture
   {
   public:
      /**
       * Creats an empty wrapper.
       */
      WExceptionCapture ();

      //@{
      /**
       * Creates a wrapper that contains the given exception.
       *
       * @param exc The exception to capture.
       *
       * @param at The current transaction.
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
      //@}
      
      //@{
      /**
       * Copies/moves the error from the given wrapper into this one.
       *
       * @param exc The object to copy or move from.
       */
      WExceptionCapture (const WExceptionCapture& exc);
      WExceptionCapture& operator= (const WExceptionCapture& exc);

      WExceptionCapture (WExceptionCapture&& exc);
      WExceptionCapture& operator=(WExceptionCapture&& exc);
      //@}
      
      //@{
      /**
       * Captures the given exception. If the object to capture is another WExceptionCapture or
       * WExceptionCapturethen the object captured by exc will be captured instead.
       *
       * @param exc The exception to capture, must be copyable.
       *
       * @param at The current transaction.
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
      //@}

      //@{
      /**
       * Clears out any captured exceptions.
       *
       * @param at The current transaction.
       */
      void Reset ();
      void Reset (WAtomic& at);
      //@}
      
      //@{
      /**
       * Throws the wrapped exception. Does nothing if there has been no exception captured.
       * 
       * @param at The current transaction.
       */
      void ThrowCaptured () const;
      void ThrowCaptured (WAtomic& at) const;
      //@}

      //@{
      /**
       * Checks if an exception has been captured.
       * 
       * @param at The STM transaction to use.
       *
       * @returns true if an exception has been captured, false otherwise.
       */
      operator bool () const;
      bool HasCaptured (WAtomic& at) const;
      //@}
      
   private:
      typedef std::function<void ()> Thrower;
      WVar<Thrower> m_thrower_v;
   };

   ///@}

}

