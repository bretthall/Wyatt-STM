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

#include "exception_capture.h"

namespace WSTM
{
   WExceptionCapture::WExceptionCapture ()
   {}

   WExceptionCapture::WExceptionCapture (const WExceptionCapture& exc):
      m_thrower_v (exc.m_thrower_v.GetReadOnly ())
   {}

   WExceptionCapture& WExceptionCapture::operator= (const WExceptionCapture& exc)
   {
      Atomically ([&](WAtomic& at)
                  {
                     m_thrower_v.Set (exc.m_thrower_v.Get(at), at);
                  });
      return *this;
   }

   WExceptionCapture::WExceptionCapture (WExceptionCapture&& exc):
      m_thrower_v (std::move (exc.m_thrower_v))
   {}

   WExceptionCapture& WExceptionCapture::operator= (WExceptionCapture&& exc)
   {
      m_thrower_v = std::move (exc.m_thrower_v);
      return *this;
   }

   void WExceptionCapture::Capture (const WExceptionCapture& exc)
   {
      Atomically ([&](WAtomic& at){Capture (exc, at);});
   }
   
   void WExceptionCapture::Capture (const WExceptionCapture& exc, WAtomic& at)
   {
      m_thrower_v.Set (exc.m_thrower_v.Get (at), at);
   }

   void WExceptionCapture::Reset ()
   {
      SetVar (m_thrower_v, Thrower ());
   }
   
   void WExceptionCapture::Reset (WAtomic& at)
   {
      m_thrower_v.Set (Thrower (), at);
   }

   void WExceptionCapture::ThrowCaptured () const
   {
      Atomically ([&](WAtomic& at){ThrowCaptured (at);});
   }
   
   void WExceptionCapture::ThrowCaptured (WAtomic& at) const
   {
      Thrower thrower = m_thrower_v.Get (at);
      if (thrower)
      {
         thrower ();
      }
   }

   WExceptionCapture::operator bool () const
   {
      return bool (m_thrower_v.GetReadOnly ());
   }
   
   bool WExceptionCapture::HasCaptured(WAtomic& at) const
   {
      return bool (m_thrower_v.Get (at));
   }

}
