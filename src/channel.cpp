/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2010. All rights reserved.
****************************************************************************/

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
