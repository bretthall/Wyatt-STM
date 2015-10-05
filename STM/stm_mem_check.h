/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2011. All rights reserved.
****************************************************************************/

#pragma once

//#define DEBUG_ATOMIC_MEM

namespace bss
{
   namespace thread
   {
      namespace STM
      {
         //These functions defined in stm.cpp.
         void BSS_LIBAPI CheckAtomicMem (std::vector<std::pair<size_t, int> >& result);
         void BSS_LIBAPI ClearAtomicMem ();
      }
   }
}
