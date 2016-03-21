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
#include "maybe_unused.h"

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

      ptrdiff_t FrameSize (const WFrameHeader* header_p)
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
   {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
      m_data.fill (0);
#endif
   }

   const uint8_t* WPage::GetData () const
   {
      return m_data.data () + pagePadding;
   }

   size_t WPage::GetUsed () const
   {
      return m_used;
   }
   
   size_t WPage::GetLeft () const
   {
      return pageSize - m_used;
   }
   
   uint8_t* WPage::Reserve (const size_t numBytes)
   {
      if (numBytes <= GetLeft ())
      {
         auto pos_p = m_data.data () + pagePadding + m_used;
         m_used += numBytes;
         return pos_p;
      }
      else
      {
         return nullptr;
      }
   }
         
   WPage* WPage::NewPage ()
   {
      if (!m_next_p)
      {
         m_next_p = std::make_unique<WPage>();
      }
      CheckIntegrity ();
      return m_next_p.get ();
   }
   
   std::unique_ptr<WPage> WPage::ReleaseNext ()
   {
      return std::move (m_next_p);
   }

   void WPage::Capture (std::unique_ptr<WPage>&& next_p)
   {
      assert (!m_next_p);
      if (next_p && (next_p->m_used != 0))
      {
         //don't bother saving the pages if they're empty
         m_next_p = std::move (next_p);
      }
   }
   
   void WPage::CheckIntegrity () const
   {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
      for (auto i = 0; i < pagePadding; ++i)
      {
         assert (m_data[i] == 0);
      }

      for (auto i = (m_used + pagePadding); i < (pageSize + 2*pagePadding); ++i)
      {
         assert (m_data[i] == 0);
      }
#endif
   }

   void WPage::Clear ()
   {
#ifdef WSTM_CONFLICT_PROFILING_INTEGRITY_CHECKING
      m_data.fill (0);
#endif
      m_used = 0;
      if (m_next_p)
      {
         m_next_p->Clear ();
      }
   }

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

      auto skipBytes = ptrdiff_t (0);
      std::unique_ptr<WPage> cur_p = std::move (m_firstPage_p);
      while (cur_p)
      {
         auto data_p = cur_p->GetData ();
         out.write (reinterpret_cast<const char*>(data_p), cur_p->GetUsed ());

         const auto dataEnd_p = data_p + cur_p->GetUsed ();
         auto bytesLeft = (dataEnd_p - data_p);
         if (bytesLeft >= skipBytes)
         {
            data_p += skipBytes;
            bytesLeft -= skipBytes;
            while (data_p < dataEnd_p)
            {
               auto frame_p = reinterpret_cast<const Frames::WFrameHeader*>(data_p);
               names.insert (frame_p->m_name);
               skipBytes = FrameSize (frame_p);
               if (bytesLeft >= skipBytes)
               {
                  data_p += skipBytes;
                  bytesLeft -= skipBytes;
                  skipBytes = 0;
               }
               else
               {
                  skipBytes -= bytesLeft;
                  data_p = dataEnd_p;
               }
            }
         }
         else
         {
            skipBytes -= bytesLeft;
         }            

         cur_p = cur_p->ReleaseNext ();
      }

      auto nameData = Frames::WNameData ();
      nameData.m_type = Frames::FrameType::nameData;
      for (const auto name_p: names)
      {
         if (name_p != nullptr)
         {
            nameData.m_name = name_p;
            nameData.m_numChars = strlen (name_p);
            out.write (reinterpret_cast<const char*>(&nameData), sizeof(Frames::WNameData));
            out.write (name_p, nameData.m_numChars);
         }
      }
   }

   void WMainData::NewThread ()
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);
      ++m_numThreads;
   }
   
   void WMainData::ThreadDone (std::unique_ptr<WPage>&& first_p, WPage* last_p)
   {
      auto lock = std::unique_lock<std::mutex>(m_mutex);

      if (first_p)
      {
         assert (last_p != nullptr);
         
         if (m_lastPage_p)
         {
            m_lastPage_p->Capture (std::move (first_p));
         }
         else
         {
            assert (!m_firstPage_p);
            m_firstPage_p = std::move (first_p);
         }
         m_lastPage_p = last_p;
      }
      else
      {
         assert (last_p == nullptr);
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
      //don't need mutex lock here, m_clearIndex is atomic
      return m_clearIndex;
   }
   
   WThreadData::WThreadData (WMainData& mainData):
      m_mainData (mainData),
      m_curPage_p (nullptr),
      m_clearIndex (mainData.GetClearIndex ()),
      m_threadName (nullptr),
      m_curTransactionFile (nullptr),
      m_curTransactionLine (-1),
      m_curTransactionName (nullptr),
      m_inChildTransaction (0)
   {
      m_mainData.NewThread ();
   }

   WThreadData::~WThreadData ()
   {
      m_mainData.ThreadDone (std::move (m_firstPage_p), m_curPage_p);
   }
   
   void WThreadData::NameThread (const char* name)
   {
      assert (m_threadName == nullptr);
      m_threadName = name;
   }

   ConflictProfiling::Internal::WOnTransactionEnd WThreadData::StartTransaction (const char* file, const int line)
   {
      //WHat about sub-transactions??????????????????????????????????????????????????????????????????????????
      // -> array of transaction trackers, output "sub-transaction" frames for sub-transactions,
      //sub-transaction frames will need both get and set vars since we don't know if the top-level
      //will have a conflict or not
      // -> probably be better to only report data for top-level transactions

      //=> Currently only dealing with top-level transactions, we ignore child transactions, their
      //read and write sets will be recorded when the top-level transaction commits or aborts
#ifdef WSTM_CONFLICT_PROFILING
      if (m_inChildTransaction == 0)
      {
         m_curTransactionFile = file;
         m_curTransactionLine = line;
         m_curTransactionName = nullptr;
      }
      ++m_inChildTransaction;
      return ConflictProfiling::Internal::WOnTransactionEnd (&m_inChildTransaction);
#else
      Internal::MaybeUnused (file, line);
      return ConflictProfiling::Internal::WOnTransactionEnd ();
#endif
   }

   void WThreadData::StartTransactionAttempt ()
   {
      if (NotInChildTransaction ())
      {
         m_curTransactionStart = std::chrono::high_resolution_clock::now ();
      }
   }

   void WThreadData::TransactionEnd (const Frames::FrameType type, const std::chrono::high_resolution_clock::time_point end, const Internal::VarMap& vars)
   {
      Internal::MaybeUnused (type, end, vars);
      
      if (NotInChildTransaction ())
      {
         const auto oldPage_p = m_curPage_p;
         if (oldPage_p)
         {
            oldPage_p->CheckIntegrity ();
         }

         auto dest_p = GetNextDest (sizeof(Frames::WTransaction));
         auto frame_p = reinterpret_cast<Frames::WTransaction*>(dest_p);
         frame_p->m_type = type;
         frame_p->m_name = m_curTransactionName;
         frame_p->m_threadName = m_threadName;
         frame_p->m_start = m_curTransactionStart;
         frame_p->m_end = end;
         frame_p->m_file = m_curTransactionFile;
         frame_p->m_line = static_cast<uint16_t>(std::max (static_cast<int>(std::numeric_limits<uint16_t>::max ()), m_curTransactionLine));
         frame_p->m_numVars = static_cast<uint16_t>(vars.size ());
         m_curPage_p->CheckIntegrity ();
         
         //we might not be able to fit all the vars on one page, split them up if need be
         auto varMemLeft = sizeof(void*)*vars.size ();
         auto varsIt = std::begin (vars);
         while (varMemLeft > 0)
         {
            assert (varsIt != std::end (vars));
            
            auto memToUse = std::min (varMemLeft, m_curPage_p->GetLeft ());
            auto numThatFit = memToUse/sizeof(void*);
            if (numThatFit == 0)
            {
               m_curPage_p = m_curPage_p->NewPage ();
               memToUse = std::min (varMemLeft, m_curPage_p->GetLeft ());
               numThatFit = memToUse/sizeof(void*);
            }
            auto vars_p = reinterpret_cast<void**>(m_curPage_p->Reserve (memToUse));
            auto varsEnd = varsIt;
            std::advance (varsEnd, numThatFit);
            std::transform (varsIt, varsEnd,
                            vars_p,
                            [](const auto& val) -> void*
                            {
                               return std::get<0>(val).get ();
                            });
            m_curPage_p->CheckIntegrity ();
            varsIt = varsEnd;
            varMemLeft -= memToUse;
         }

         m_curPage_p->CheckIntegrity ();
      }
      //NOTE: decrementing m_inChildTransaction is handled by the WTransactionEnd returned by StartTransaction
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
      //only the top-level transaction can set the name
      if (NotInChildTransaction ())
      {
         m_curTransactionName = name;
      }
   }
   
   void WThreadData::NameVar (void* var_p, const char* name)
   {
      auto dest_p = GetNextDest (sizeof(Frames::WVarName));
      auto frame_p = reinterpret_cast<Frames::WVarName*>(dest_p);
      frame_p->m_type = Frames::FrameType::varName;
      frame_p->m_var_p = var_p;
      frame_p->m_name = name;
      m_curPage_p->CheckIntegrity ();
   }

   uint8_t* WThreadData::GetNextDest (const size_t size)
   {
      if (m_clearIndex != m_mainData.GetClearIndex ())
      {
         m_firstPage_p->Clear ();
      }

      if (m_curPage_p)
      {
         if (m_curPage_p->GetLeft () < size)
         {
            m_curPage_p = m_curPage_p->NewPage ();
         }
      }
      else
      {
         m_firstPage_p = std::make_unique<WPage>();
         m_curPage_p = m_firstPage_p.get ();
      }

      return m_curPage_p->Reserve (size);
   }

   bool WThreadData::NotInChildTransaction () const
   {
      return m_inChildTransaction == 1;
   }

   namespace
   {
      WVarName ConvertVarName (const Frames::WFrameHeader* header_p)
      {
         assert (header_p->m_type == Frames::FrameType::varName);
         auto frame_p = reinterpret_cast<const Frames::WVarName*>(header_p);
         auto data = WVarName ();
         data.m_var_p = frame_p->m_var_p;
         data.m_name = frame_p->m_name;
         return data;
      }

      WConflict ConvertConflict (const Frames::WFrameHeader* header_p)
      {
         assert (header_p->m_type == Frames::FrameType::conflict);
         auto frame_p = reinterpret_cast<const Frames::WTransaction*>(header_p);
         auto data = WConflict ();
         data.m_transactionName = frame_p->m_name;
         data.m_threadName = frame_p->m_threadName;
         data.m_start = frame_p->m_start;
         data.m_end = frame_p->m_end;
         data.m_file = frame_p->m_file;
         data.m_line = frame_p->m_line;
         auto var_p = reinterpret_cast<const void* const*>(reinterpret_cast<const uint8_t*>(frame_p) + sizeof(Frames::WTransaction));
         data.m_got = std::vector<const void*>(var_p, var_p + frame_p->m_numVars);
         return data;
      }

      WCommit ConvertCommit (const Frames::WFrameHeader* header_p)
      {
         assert (header_p->m_type == Frames::FrameType::commit);
         auto frame_p = reinterpret_cast<const Frames::WTransaction*>(header_p);
         auto data = WCommit ();
         data.m_transactionName = frame_p->m_name;
         data.m_threadName = frame_p->m_threadName;
         data.m_start = frame_p->m_start;
         data.m_end = frame_p->m_end;
         data.m_file = frame_p->m_file;
         data.m_line = frame_p->m_line;
         auto var_p = reinterpret_cast<const void* const*>(reinterpret_cast<const uint8_t*>(frame_p) + sizeof(Frames::WTransaction));
         data.m_set = std::vector<const void*>(var_p, var_p + frame_p->m_numVars);
         return data;
      }

      WName ConvertNameData (const Frames::WFrameHeader* header_p)
      {
         assert (header_p->m_type == Frames::FrameType::nameData);
         auto frame_p = reinterpret_cast<const Frames::WNameData*>(header_p);
         auto data = WName ();
         data.m_key = frame_p->m_name;
         auto chars_p = reinterpret_cast<const char*>(frame_p) + sizeof(Frames::WNameData);
         data.m_name = std::string(chars_p, chars_p + frame_p->m_numChars);
         return data;
      }
   }

   std::tuple<WData, const uint8_t*> ConvertData (const uint8_t* start_p, const uint8_t* end_p)
   { 
     auto header_p = reinterpret_cast<const Frames::WFrameHeader*>(start_p);

     //first we need to make sure that we have enough data to determine the frame size
     auto minSize = ptrdiff_t (0);
     switch(header_p->m_type)
     {
     case Frames::FrameType::varName:
        minSize = sizeof(Frames::WVarName);
        break;
        
     case Frames::FrameType::commit:
     case Frames::FrameType::conflict:
        minSize = sizeof(Frames::WTransaction);
        break;
        
     case Frames::FrameType::nameData:
        minSize = sizeof(Frames::WNameData);
        break;

     default:
        //if we get here it means we added a frame type and didn't include it in this function        
        assert (false);
        return std::make_tuple (WIncomplete {0}, start_p);
     };
     const auto maxSize = end_p - start_p;
     if (maxSize < minSize)
     {
        return std::make_tuple (WIncomplete {minSize}, start_p);
     }

     //We have enough data to determine the full frame size
     auto size = Frames::FrameSize (header_p);
     if (maxSize < size)
     {
        return std::make_tuple (WIncomplete {size}, start_p);
     }
     
     //we've got a full frame
     switch(header_p->m_type)
     {
     case Frames::FrameType::varName:
        return std::make_tuple (ConvertVarName (header_p), start_p + size);
        
     case Frames::FrameType::commit:
        return std::make_tuple (ConvertCommit (header_p), start_p + size);
        
     case Frames::FrameType::conflict:
        return std::make_tuple (ConvertConflict (header_p), start_p + size);
        
     case Frames::FrameType::nameData:
        return std::make_tuple (ConvertNameData (header_p), start_p + size);

     default:
        //if we get here it means we added a frame type and didn't include it in this function        
        assert (false);
        return std::make_tuple (WIncomplete {0}, start_p);
     };
   }

}}

