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

#include "channel.h"

namespace WSTM
{
   WChannelError::WChannelError (const std::string& msg):
      m_msg (msg)
   {}

   WInvalidChannelError::WInvalidChannelError():
      WChannelError("Attempt to use an invalid channel")
   {}

   namespace Internal
   {
#ifdef WATCH_MEMORY
      namespace
      {
         int s_numNodes = 0;
         std::mutex s_numNodesMutex;
         size_t s_nodeNum = 1;
         using NodeMap = std::map<WChannelCoreNodeBase*, size_t>;
         NodeMap s_nodeNums;
      }

      void IncrementNumNodes (WChannelCoreNodeBase* node_p)
      {
         std::lock_guard<std::mutex> lock (s_numNodesMutex);
         ++s_numNodes;
         s_nodeNums[node_p] = s_nodeNum;
         ++s_nodeNum;
      }
      
      void DecrementNumNodes (WChannelCoreNodeBase* node_p)
      {
         std::lock_guard<std::mutex> lock (s_numNodesMutex);
         --s_numNodes;
         assert (s_numNodes >= 0);
         s_nodeNums[node_p] = 0;
      }

      int GetNumNodes ()
      {
         std::lock_guard<std::mutex> lock (s_numNodesMutex);
         return s_numNodes;
      }

      std::vector<size_t> GetExistingNodeNums ()
      {
         std::lock_guard<std::mutex> lock (s_numNodesMutex);
         std::vector<size_t> nums;
         for (const auto& val: s_nodeNums)
         {
            if (val.second > 0)
            {
               nums.push_back (val.second);
            }
         }
         return nums;
      }

      size_t GetMaxNodeNum ()
      {
         std::lock_guard<std::mutex> lock (s_numNodesMutex);
         return s_nodeNum;
      }

#else //WATCH_MEMORY

      void IncrementNumNodes (WChannelCoreNodeBase*)
      {}
      
      void DecrementNumNodes (WChannelCoreNodeBase*)
      {}

      int GetNumNodes ()
      {
         return 0;
      }

      std::vector<size_t> GetExistingNodeNums ()
      {
         return std::vector<size_t>();
      }

      size_t GetMaxNodeNum ()
      {
         return 0;
      }

#endif //WATCH_MEMORY
   }
   
}
