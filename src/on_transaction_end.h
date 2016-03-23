/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2016. All rights reserved.
****************************************************************************/

#pragma once

namespace WSTM
{
   namespace ConflictProfiling
   {
      namespace Internal
      {
         class WOnTransactionEnd
         {
            //NOTE: this class is implemented in conflict_profiling_internal.cpp
         public:
#ifdef WSTM_CONFLICT_PROFILING
            explicit WOnTransactionEnd (unsigned int* inChildTransaction);
            
            WOnTransactionEnd (WOnTransactionEnd&& e);
            WOnTransactionEnd& operator= (WOnTransactionEnd&& e);
            
            ~WOnTransactionEnd ();
#endif
            
         private:
#ifdef WSTM_CONFLICT_PROFILING
            unsigned int* m_inChildTransaction_p;
#endif
         };
      }
   }
}
