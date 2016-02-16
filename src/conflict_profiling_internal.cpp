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

#include "conflict_profiling_internal.h"

#include <boost/format.hpp>
#include <boost/range/algorithm/transform.hpp>

#include <limits>
#include <unordered_set>
#include <fstream>
#include <algorithm>

namespace WSTM { namespace ConflictProfilingInternal
{
   namespace Frames
   {
      struct WFrameHeader
      {
         FrameType m_type;
         const char* m_name;
      };
      static_assert (sizeof(WFrameHeader) == 4 + sizeof(char*), "Bad WFrameHeader size");
      
      struct WThreadName : WFrameHeader
      {};

      struct WVarName : WFrameHeader
      {
         void* m_var_p;
      };
      static_assert (sizeof(WVarName) == sizeof(WFrameHeader) + sizeof(void*), "Bad WVarName size");

      struct WTransaction : WFrameHeader
      {
         const char* m_threadName;
         std::chrono::high_resolution_clock::time_point m_start;
         std::chrono::high_resolution_clock::time_point m_end;
         const char* m_file;
         uint16_t m_line;
         uint16_t m_numVars;
         //will be followed by m_numVars void*'s
      };
      static_assert (sizeof(WTransaction) ==
                     (sizeof(WFrameHeader)
                      + 2*sizeof(std::chrono::high_resolution_clock::time_point)
                      + 2*sizeof(char*)
                      + 2*sizeof(uint16_t) + 4), "Bad WTransaction size");

      struct WNameData : WFrameHeader
      {
         uint32_t m_numChars;
         //will be followed by m_numChars char's
      };
      static_assert (sizeof(WNameData) == sizeof(WFrameHeader) + sizeof(uint32_t), "Bad WNameData size");

      size_t FrameSize (const WFrameHeader* header_p)
      {
         switch(header_p->m_type)
         {
         case FrameType::varName:
            return sizeof(WVarName);

         case FrameType::commit:
         case FrameType::conflict:
         {
            const auto frame_p = reinterpret_cast<const WTransaction*>(header_p);
            return (sizeof(WTransaction) + frame_p->m_numVars*sizeof(void*));
         }

         case FrameType::nameData:
         {
            const auto frame_p = reinterpret_cast<const WNameData*>(header_p);
            return (sizeof(WNameData) + frame_p->m_numChars);
         }
            
         default:
            //if we get here it means we added a frame type and didn't include it in this function
            assert (false);
            return(0);
         };
      }
      
   }

   WPage::WPage ():
      m_used (0)
   {}

   WMainData::WMainData ():
      m_lastPage_p (nullptr),
      m_numThreads (0),
      m_clearIndex (0)
   {}

   WMainData::~WMainData ()
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);
      while (m_numThreads > 0)
      {
         m_numThreadsCond.wait (lock);
      }

      auto names = std::unordered_set<const char*> ();

      const auto filename = boost::str (boost::format ("wstm_%1%.profile") % std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ()));
      auto out = std::ofstream (filename, std::ios::out | std::ios::binary);
         
      std::unique_ptr<WPage> cur_p = std::move (m_firstPage_p);
      while (cur_p)
      {
         auto data_p = cur_p->m_data.data ();
         out.write (reinterpret_cast<const char*>(data_p), cur_p->m_used);

         const auto dataEnd_p = data_p + cur_p->m_used;
         while (data_p < dataEnd_p)
         {
            auto frame_p = reinterpret_cast<const Frames::WFrameHeader*>(data_p);
            names.insert (frame_p->m_name);
            data_p += FrameSize (frame_p);
         }

         cur_p = std::move (cur_p->m_next_p);
      }

      auto nameData = Frames::WNameData ();
      nameData.m_type = Frames::FrameType::nameData;
      for (const auto name_p: names)
      {
         nameData.m_name = name_p;
         nameData.m_numChars = std::max (std::numeric_limits<uint32_t>::max (), strlen (name_p));
         out.write (reinterpret_cast<const char*>(&nameData), sizeof(Frames::WNameData));
         out.write (name_p, nameData.m_numChars);
      }
   }

   void WMainData::NewThread ()
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);
      ++m_numThreads;
   }
   
   void WMainData::ThreadDone (std::unique_ptr<WPage>&& data_p)
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);
         
      if (m_lastPage_p)
      {
         m_lastPage_p->m_next_p = std::move (data_p);
      }
      else
      {
         m_firstPage_p = std::move (data_p);
         m_lastPage_p = m_firstPage_p.get ();
      }
      while (m_lastPage_p->m_next_p)
      {
         m_lastPage_p = m_lastPage_p->m_next_p.get ();
      }
      
      --m_numThreads;
      m_numThreadsCond.notify_one ();
   }

   void WMainData::Clear ()
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);
      ++m_clearIndex;
      m_firstPage_p = nullptr;
      m_lastPage_p = nullptr;
   }

   size_t WMainData::GetClearIndex () const
   {
      return m_clearIndex;
   }

   const WPage* WMainData::GetFirstPage () const
   {
      return m_firstPage_p.get ();
   }
   
   WThreadData::WThreadData (WMainData& mainData):
      m_mainData (mainData),
      m_curPage_p (nullptr),
      m_clearIndex (mainData.GetClearIndex ()),
      m_threadName (nullptr),
      m_curTransactionFile (nullptr),
      m_curTransactionLine (-1),
      m_curTransactionName (nullptr)
   {
      m_mainData.NewThread ();
   }

   WThreadData::~WThreadData ()
   {
      m_mainData.ThreadDone (std::move (m_firstPage_p));
   }
   
   void WThreadData::NameThread (const char* name)
   {
      assert (m_threadName == nullptr);
      m_threadName = name;
   }
      
   void WThreadData::StartTransaction (const char* file, const int line)
   {
      //WHat about sub-transactions??????????????????????????????????????????????????????????????????????????
      // -> array of transaction trackers, output "sub-transaction" frames for sub-transactions,
      //sub-transaction frames will need both get and set vars since we don't know if the top-level
      //will have a conflict or not
      // -> probably be better to only report data for top-level transactions
      m_curTransactionFile = file;
      m_curTransactionLine = line;
      m_curTransactionName = nullptr;
      m_curTransactionStart = std::chrono::high_resolution_clock::now ();
   }

   void WThreadData::TransactionEnd (const Frames::FrameType type, const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& vars)
   {
      auto dest_p = GetNextDest (sizeof(Frames::WTransaction) + sizeof(void*)*vars.size ());
      auto frame_p = reinterpret_cast<Frames::WTransaction*>(dest_p);
      frame_p->m_type = type;
      frame_p->m_name = m_curTransactionName;
      frame_p->m_threadName = m_threadName;
      frame_p->m_start = m_curTransactionStart;
      frame_p->m_end = end;
      frame_p->m_file = m_curTransactionFile;
      frame_p->m_line = static_cast<uint16_t>(std::max (static_cast<int>(std::numeric_limits<uint16_t>::max ()), m_curTransactionLine));
      frame_p->m_numVars = static_cast<uint16_t>(vars.size ());
      auto vars_p = reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(frame_p) + sizeof(Frames::WTransaction));
      boost::transform (vars, vars_p,
                        [](const auto& val) -> void*
                        {
                           return std::get<0>(val).get ();
                        });
   }      

   void WThreadData::Commit (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& setVars)
   {
      TransactionEnd (Frames::FrameType::commit, end, setVars);
   }
   
   void WThreadData::Conflict (const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& getVars)
   {
      TransactionEnd (Frames::FrameType::conflict, end, getVars);
   }

   void WThreadData::NameTransaction (const char* name)
   {
      m_curTransactionName = name;
   }
   
   void WThreadData::NameVar (void* var_p, const char* name)
   {
      auto dest_p = GetNextDest (sizeof(Frames::WVarName));
      auto frame_p = reinterpret_cast<Frames::WVarName*>(dest_p);
      frame_p->m_type = Frames::FrameType::varName;
      frame_p->m_var_p = var_p;
      frame_p->m_name = name;
   }

   uint8_t* WThreadData::GetNextDest (const size_t size)
   {
      if (m_clearIndex != m_mainData.GetClearIndex ())
      {
         m_curPage_p = m_firstPage_p.get ();
         auto cur_p = m_curPage_p;
         while (cur_p)
         {
            cur_p->m_used = 0;
            cur_p = cur_p->m_next_p.get ();
         }
      }
      
      if (m_curPage_p)
      {
         if ((pageSize - m_curPage_p->m_used) < size)
         {
            if (!m_curPage_p->m_next_p)
            {
               m_curPage_p->m_next_p = std::make_unique<WPage>();
            }
            m_curPage_p = m_curPage_p->m_next_p.get ();
         }
      }
      else
      {
         m_firstPage_p = std::make_unique<WPage>();
         m_curPage_p = m_firstPage_p.get ();
      }

      const auto dest_p = m_curPage_p->m_data.data () + m_curPage_p->m_used;
      m_curPage_p->m_used += size;
      return dest_p;
   }
   
}}

