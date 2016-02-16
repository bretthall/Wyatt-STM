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

#include <chrono>
#include <memory>
#include <array>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace WSTM
{
   namespace ConflictProfilingInternal
   {
      constexpr auto kb = size_t (1024);
      constexpr auto pageSize = size_t (4*kb);

      struct WPage
      {
         WPage ();
         
         std::unique_ptr<WPage> m_next_p;
         size_t m_used;
         std::array<uint8_t, pageSize> m_data;
      };

      class WMainData
      {
      public:
         WMainData ();
         ~WMainData ();

         void NewThread ();
         void ThreadDone (std::unique_ptr<WPage>&& data_p);

         void Clear ();

         size_t GetClearIndex () const;
         
         const WPage* GetFirstPage () const;
         
      private:
         std::mutex m_mutex;
         std::unique_ptr<WPage> m_firstPage_p;
         WPage* m_lastPage_p;
         size_t m_numThreads;
         std::condition_variable m_numThreadsCond;
         std::atomic<size_t> m_clearIndex;
      };

      namespace Frames
      {

         enum class FrameType : uint8_t
         {
            varName = 0,
            commit,
            conflict,
            nameData
         };
      }
      
      class WThreadData
      {
      public:
         WThreadData (WMainData& mainData);
         ~WThreadData ();

         void NameThread (const char* name);
         void StartTransaction (const char* file, const int line);
         void Commit (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& setVars);
         void Conflict (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& getVars);
         void NameTransaction (const char* name);
         void NameVar (void* core_p, const char* name);
         
      private:
         void TransactionEnd (const Frames::FrameType type, const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& vars);
         uint8_t* GetNextDest (const size_t size);

         WMainData& m_mainData;
         std::unique_ptr<WPage> m_firstPage_p;
         WPage* m_curPage_p;
         size_t m_clearIndex;

         const char* m_threadName;
         const char* m_curTransactionFile;
         int m_curTransactionLine;
         const char* m_curTransactionName;
         std::chrono::high_resolution_clock::time_point m_curTransactionStart;
      };

      
      struct WVarName
      {
         void* m_var_p;
         const char* m_name;
      };
      
      struct WConflict
      {
         const char* m_name;
         std::chrono::high_resolution_clock::time_point m_start;
         std::chrono::high_resolution_clock::time_point m_end;
         std::vector<void*> m_got;
      };
      
      struct WCommit
      {
         const char* m_name;
         std::chrono::high_resolution_clock::time_point m_start;
         std::chrono::high_resolution_clock::time_point m_end;
         std::vector<void*> m_set;
      };

   }
}

