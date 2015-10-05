/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "stdafx.h"

#pragma warning (disable: 4503)

#include "BSS/Thread/STM/stm.h"
#include "BSS/Thread/STM/stm_mem_check.h"
#include "BSS/Common/ExitOperation.h"

#include "BSS/Logger/Logging.h"

using boost::shared_ptr;

using boost::thread_specific_ptr;
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/thread_time.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
using boost::format;
using boost::str;
#include <boost/bind.hpp>
using boost::bind;
#include <boost/atomic.hpp>

#undef min
#undef max
#include <limits>
#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>
//Define this to turn on stm profiling
//#define STM_PROFILING

#ifdef STM_PROFILING
#include <windows.h>
#endif

namespace bss { namespace thread { namespace  STM
{

   namespace
   {
      bss::logging::WLogger& memlogger = bss::logging::getLogger ("bss.thread.STM.Memory");
      bss::logging::WLogger& runLockedLogger = bss::logging::getLogger ("bss.thread.STM.RunLocked");

//#define TRACK_INITED_THREADS      
#ifdef TRACK_INITED_THREADS

#ifdef WIN32
      boost::mutex s_initedThreadsMutex;
      std::vector<DWORD> s_initedThreads;
      std::vector<DWORD> s_exittedThreads;

      void TrackThreadInit ()
      {
         boost::lock_guard<boost::mutex> lock (s_initedThreadsMutex);
         const DWORD i = ::GetCurrentThreadId ();
         std::vector<DWORD>& initedThreads = s_initedThreads;
         if (!std::binary_search (initedThreads.begin (), initedThreads.end (), i))
         {
            initedThreads.push_back (i);
            std::sort (initedThreads.begin (), initedThreads.end ());
         }
      }

      void CheckThreadInited ()
      {
         boost::lock_guard<boost::mutex> lock (s_initedThreadsMutex);
         const DWORD i = ::GetCurrentThreadId ();
         const std::vector<DWORD>& initedThreads = s_initedThreads;
         assert (std::binary_search (initedThreads.begin (), initedThreads.end (), i));
      }

      void TrackThreadExit ()
      {
         boost::lock_guard<boost::mutex> lock (s_initedThreadsMutex);
         const DWORD i = ::GetCurrentThreadId ();
         std::vector<DWORD>& exittedThreads = s_exittedThreads;
         if (!std::binary_search (exittedThreads.begin (), exittedThreads.end (), i))
         {
            exittedThreads.push_back (i);
            std::sort (exittedThreads.begin (), exittedThreads.end ());
         }
      }

      void CheckThreadNotExitted ()
      {
         boost::lock_guard<boost::mutex> lock (s_initedThreadsMutex);
         const DWORD i = ::GetCurrentThreadId ();
         const std::vector<DWORD>& exittedThreads = s_exittedThreads;
         assert (!std::binary_search (exittedThreads.begin (), exittedThreads.end (), i));
      }
      
#endif //WIN32
      
#else
      void TrackThreadInit ()
      {}

      void CheckThreadInited ()
      {}

      void TrackThreadExit ()
      {}

      void CheckThreadNotExitted ()
      {}

#endif //TRACK_INITED_THREADS

   }

   namespace Internal
   {
      void LogMemoryAllocation (const char* typeName,
                                const void* address,
                                const char* filename,
                                const int line)
      {
         LOG_DEBUG (memlogger, str (format ("%1% allocated at %2% (%3%:%4%)")
                                    % typeName % address % filename % line));
      }
   
      void LogMemoryDeallocation (const char* typeName,
                                  const void* address,
                                  const char* filename,
                                  const int line)
      {
         LOG_DEBUG (memlogger, str (format ("%1% deallocated at %2% (%3%:%4%)")
                                    % typeName % address % filename % line));
      }

      WLocalValueBase::~WLocalValueBase ()
      {}

      uint64_t GetTransactionLocalKey ()
      {
#pragma warning(push)
#pragma warning(disable: 4640)
         static boost::atomic<uint64_t> nextKey (0);
#pragma warning(pop)
         return nextKey.fetch_add (1);
      }

      //We get a key here to force the initialization of nextKey in GetTransactionLocalKey during
      //what should be a time when no other threads should be calling it. Were we using a C++11
      //compliant compiler where function scope static variable initialization is thread-safe then
      //we wouldn't need to do this. With our current compiler there is a small chance that two
      //threads could both create WTransactionLocalValue objects at the same time and have issues
      //with nextKey initialization. During program start-up we aren't using multiple threads
      //anywhere so it should be safe to initialize nextKey. Soon we'll be switching to VS2015 which
      //has thread-safe static initialization and all this will be unescessary.
      const auto s_unusedLocalKey = GetTransactionLocalKey ();
   }
   
   namespace
   {
#ifdef STM_PROFILING
      boost::mutex s_profileMutex;
      boost::posix_time::ptime s_profileStart;
   
      //this would be better done using c++ std atomics but we don't have
      //those yet so for now we have to be WIN32 specific
      long s_numConflicts;
      long s_numReadCommits;
      long s_numWriteCommits;

#endif //STM_PROFILING

      void IncrementNumConflicts ()
      {
#ifdef STM_PROFILING
         InterlockedIncrementRelease (&s_numConflicts);
#endif
      }
   
      void IncrementNumReadCommits ()
      {
#ifdef STM_PROFILING
         InterlockedIncrementRelease (&s_numReadCommits);
#endif
      }

      void IncrementNumWriteCommits ()
      {
#ifdef STM_PROFILING
         InterlockedIncrementRelease (&s_numWriteCommits);
#endif
      }
   }

   void StartProfiling ()
   {
#ifdef STM_PROFILING
      boost::unique_lock<boost::mutex> lock (s_profileMutex);
      s_profileStart = boost::posix_time::microsec_clock::local_time ();
      InterlockedExchange (&s_numConflicts, 0);
      InterlockedExchange (&s_numReadCommits, 0);
      InterlockedExchange (&s_numWriteCommits, 0);      
#endif
   }

   std::string WProfileData::FormatData () const
   {
#ifdef STM_PROFILING
      const long elapsed = (m_end - m_start).total_seconds ();
      return str (format ("\ttime = %1%secs\n"
                          "\tconflicts = %2%/sec (%3% total)\n"
                          "\treads = %4%/sec (%5% total)\n"
                          "\twrites = %6%/sec (%7% total)")
                  % elapsed
                  % (static_cast<double>(m_numConflicts)/elapsed)
                  % m_numConflicts
                  % (static_cast<double>(m_numReadCommits)/elapsed)
                  % m_numReadCommits
                  % (static_cast<double>(m_numWriteCommits)/elapsed)
                  % m_numWriteCommits);
#else
      return "\tProfiling not active";
#endif 
   }

   WProfileData Checkpoint ()
   {
      WProfileData data;
#ifdef STM_PROFILING
      data.m_numConflicts = s_numConflicts;
      data.m_numReadCommits = s_numReadCommits;
      data.m_numWriteCommits = s_numWriteCommits;
      data.m_end = boost::posix_time::microsec_clock::local_time ();
      boost::unique_lock<boost::mutex> lock (s_profileMutex);
      data.m_start = s_profileStart;
#endif      
      return data;
   }

   const unsigned int UNLIMITED = std::numeric_limits<unsigned int>::max();

   namespace
   {
      //This mutex locks out commits while a STM::Var is reading its own
      //value, while commiting this mutex is write locked so that all
      //var reads are held until the commit finishes. It is also
      //upgrade locked when a transaction is running with other
      //commits locked out.
      boost::upgrade_mutex s_readMutex;

#ifdef _DEBUG
      boost::thread_specific_ptr<bool> s_readMutexReadLocked;
      boost::thread_specific_ptr<bool> s_readMutexUpgradeLocked;
      boost::thread_specific_ptr<bool> s_readMutexWriteLocked;
      boost::thread_specific_ptr<bool> s_committing;
#endif //_DEBUG
      
      //This signal is notified when a commit succeeds. s_readMutex
      //must be used with this signal. A read lock lock should be
      //used when waiting. A write lock shoudl be held when
      //notifiying.
      boost::condition_variable_any s_commitSignal;
         
      //exception thrown by Retry() to signal AtomicallyImpl that it should
      //"retry" the current operation. 
      struct WRetryException
      {
         WRetryException(const boost::system_time& timeout):
            m_timeout(timeout)
         {}

         const boost::system_time m_timeout;
      };   
   }


   WException::WException (const std::string& msg):
      m_msg (msg)
   {}

   WCantContinueException::WCantContinueException(const std::string& msg):
      WException(msg)
   {}
   
   WAtomicallyArgs::WAtomicallyArgs():
      m_maxConflicts(UNLIMITED),
      m_conRes(WConflictResolution::THROW),
      m_maxRetries(UNLIMITED),
      m_maxRetryWait (WTimeArg::UNLIMITED ())
   {}

   namespace
   {
#ifdef _DEBUG            
      void DoLockImpl (boost::thread_specific_ptr<bool>& locked_tp)
      {
         bool* locked_p = locked_tp.get();
         if(!locked_p)
         {
            locked_p = stm_new bool;
            locked_tp.reset(locked_p);
         }
         *locked_p = true;
      }
      
      void DoUnlockImpl (boost::thread_specific_ptr<bool>& locked_tp)
      {
         bool* locked_p = locked_tp.get();
         if(!locked_p)
         {
            locked_p = stm_new bool;
            locked_tp.reset(locked_p);
         }
         *locked_p = false;
      }
#endif //_DEBUG
      
      struct WReadLockTraits
      {
         typedef boost::shared_lock<boost::upgrade_mutex> LockType;

         static void DoLock ()
         {
#ifdef _DEBUG            
            DoLockImpl (s_readMutexReadLocked);
#endif //_DEBUG
         }         

         static void DoUnlock ()
         {
#ifdef _DEBUG            
            DoUnlockImpl (s_readMutexReadLocked);
#endif //_DEBUG
         }
      };

      struct WUpgradeableLockTraits
      {
         typedef boost::upgrade_lock<boost::upgrade_mutex> LockType;

         static void DoLock ()
         {
#ifdef _DEBUG            
            DoLockImpl (s_readMutexUpgradeLocked);
#endif //_DEBUG
         }         

         static void DoUnlock ()
         {
#ifdef _DEBUG            
            DoUnlockImpl (s_readMutexUpgradeLocked);
#endif //_DEBUG
         }
      };

      template <typename LockTraits_t>
      struct WLockImpl
      {
         explicit WLockImpl(bool doLock = true);
            
         //these methods must match the boost locking convention
         void lock();
         bool try_lock();
         bool locked() const {return m_lock.owns_lock ();}
         void unlock(const int i = 1);
         
         void UnlockAll ();
         bool WaitForCommit(const boost::system_time* stop_p);
            
         int m_count;
         typedef typename LockTraits_t::LockType Lock;
         Lock m_lock;
#ifdef _DEBUG
         bool m_writeLocked;
#endif //_DEBUG
      };

      template <typename LockTraits_t>
      WLockImpl<LockTraits_t>::WLockImpl (bool doLock):
         m_count(0),
         m_lock(s_readMutex, boost::defer_lock_t())
#ifdef _DEBUG
         ,m_writeLocked(false)
#endif //_DEBUG
      {
         if(doLock)
         {
            lock();
         }
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::lock()
      {
         if(m_count == 0)
         {
            if(m_lock.owns_lock())
            {
               throw boost::lock_error();
            }
            m_lock.lock();
            LockTraits_t::DoLock ();
         }
         ++m_count;
      }

      template <typename LockTraits_t>
      bool WLockImpl<LockTraits_t>::try_lock()
      {
         if(m_count == 0)
         {
            if(m_lock.owns_lock())
            {
               throw boost::lock_error();
            }
            if(!m_lock.try_lock())
            {
               return false;
            }
            LockTraits_t::DoLock ();
         }
         ++m_count;
         return true;
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::unlock(const int i)
      {
         if(!m_lock.owns_lock())
         {
            return;
         }

         assert(m_count >= i);
         m_count -= i;
         assert(m_count >= 0);
         if(m_count == 0)
         {
            m_lock.unlock();
            LockTraits_t::DoUnlock ();
         }
      }

      template <typename LockTraits_t>
      void WLockImpl<LockTraits_t>::UnlockAll ()
      {
         unlock (m_count);
      }
      
      template <typename LockTraits_t>
      bool WLockImpl<LockTraits_t>::WaitForCommit(const boost::system_time* stop_p)
      {
         if(!m_lock.owns_lock())
         {
            throw boost::lock_error();
         }

         if(!stop_p)
         {
            s_commitSignal.wait(m_lock);
            return true;
         }
         else
         {
            return s_commitSignal.timed_wait(m_lock, *stop_p);
         }
      }

      typedef WLockImpl<WReadLockTraits> WReadLock;
      typedef WLockImpl<WUpgradeableLockTraits> WUpgradeableLock;
      
      class WWriteLock
      {
      public:
         WWriteLock(WUpgradeableLock& readLock);
         ~WWriteLock();
         
      private:
#ifdef _DEBUG
         WUpgradeableLock& m_readLock;
#endif //_DEBUG
         boost::upgrade_to_unique_lock<boost::upgrade_mutex> m_lock;
      };

      WWriteLock::WWriteLock(WUpgradeableLock& readLock):
#ifdef _DEBUG
         m_readLock (readLock),
#endif //_DEBUG
         m_lock (readLock.m_lock)
      {
#ifdef _DEBUG
         assert(!m_readLock.m_writeLocked);
         m_readLock.m_writeLocked = true;
         DoLockImpl (s_readMutexWriteLocked);
#endif //_DEBUG
      }
      
      WWriteLock::~WWriteLock()
      {
#ifdef _DEBUG
         assert(m_readLock.m_writeLocked);
         m_readLock.m_writeLocked = false;
         DoUnlockImpl (s_readMutexWriteLocked);
#endif //_DEBUG
      }

      struct WValueCoreBaseHash
      {
         size_t operator()(const Internal::WVarCoreBase::Ptr& p) const
         {
            return m_hash (p.get ());
         }

         std::hash<Internal::WVarCoreBase*> m_hash;
      };

      typedef std::unordered_map<Internal::WVarCoreBase::Ptr,
                                 Internal::WValueBase::Ptr,
                                 WValueCoreBaseHash> VarMap;
   }

   namespace Internal
   {
      //Data used for each transaction
      struct WTransactionData
      {
         WTransactionData ();
         WTransactionData (WTransactionData* parent_p);
         ~WTransactionData ();

         void Activate ();
         bool IsActive () const;
         
         WTransactionData* GetParent () const;
         WTransactionData* GetChild () const;

         int GetLevel () const;

         WReadLock& GetReadLock ();
         WUpgradeableLock& GetUpgradeLock ();

         VarMap& GetGot ();
         VarMap& GetSet ();

         Internal::WLocalValueBase* GetLocalValue (uint64_t key);
         void SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p);

         void AddBeforeCommit (WAtomic::WBeforeCommitFunc& after);
         typedef std::list<WAtomic::WBeforeCommitFunc> WBeforeCommitList;
         void GetBeforeCommits (WBeforeCommitList& beforeCommit);

         void AddAfter (WAtomic::WAfterFunc& after);
         typedef std::list<WAtomic::WAfterFunc> WAfterList;
         void GetAfters (WAfterList& afters);

         void AddOnFail (WAtomic::WOnFailFunc& after);
         typedef std::list<WAtomic::WOnFailFunc> WOnFailList;
         void RunOnFails ();
         
         void MergeToParent ();
         void MergeGetsToRoot ();
         void Clear ();
         void ClearWrites ();

         static void* const MARKER_VALUE;
         
         void* GetMarker () const;
         
      private:
         WTransactionData (const WTransactionData*);
         WTransactionData& operator=(const WTransactionData*);

         void* const m_marker;
         
         bool m_active;
         
         WTransactionData* m_parent_p;
         WTransactionData* m_child_p;
         
         //The transaction's level (1 = root transaction)
         int m_level;

         //locks for this thread.
         WReadLock m_readLock;
         WUpgradeableLock* m_upgradeLock_p;
         
         //The WVar's that have been read.
         VarMap m_got;
         //The WVar's that have been set.
         VarMap m_set;
         
         //The "transaction local" values
         std::unordered_map<uint64_t, std::unique_ptr<Internal::WLocalValueBase>> m_locals;
         
         //list of functions to run just before the top-level
         //transaction commits.
         WBeforeCommitList m_beforeCommits;

         //list of functions to run after the top-level transaction
         //commits.
         WAfterList m_afters;

         //list of functions to run if transaction fails
         WOnFailList m_onFails;
      };

      void* const WTransactionData::MARKER_VALUE = (void*)0xdeadbeef;
      
      WTransactionData::WTransactionData ():
         m_marker (MARKER_VALUE),
         m_active (false),
         m_parent_p (0),
         m_child_p (0),
         m_level (1),
         m_readLock (false),
         m_upgradeLock_p (new WUpgradeableLock (false))
      {
         LOG_MEMORY_ALLOCATION (WUpgradeableLock, m_upgradeLock_p);
      }

      WTransactionData::WTransactionData (WTransactionData* parent_p):
         m_marker (MARKER_VALUE),
         m_active (false),
         m_parent_p (parent_p),
         m_child_p (0),
         m_level (parent_p->m_level + 1),
         m_readLock (false),
         m_upgradeLock_p (parent_p->m_upgradeLock_p)
      {
         parent_p->m_child_p = this;
      }
      
      WTransactionData::~WTransactionData ()
      {
         if (!m_parent_p)
         {
            LOG_MEMORY_DEALLOCATION (WUpgradeableLock, m_upgradeLock_p);
            delete m_upgradeLock_p;
         }
      }
      
      void WTransactionData::Activate ()
      {
         m_active = true;
      }
      
      bool WTransactionData::IsActive () const
      {
         return m_active;
      }
         
      WTransactionData* WTransactionData::GetParent () const
      {
         return m_parent_p;
      }
      
      WTransactionData* WTransactionData::GetChild () const
      {
         return m_child_p;
      }

      int WTransactionData::GetLevel () const
      {
         return m_level;
      }

      WReadLock& WTransactionData::GetReadLock ()
      {
         assert (m_active);
         return m_readLock;
      }
      
      WUpgradeableLock& WTransactionData::GetUpgradeLock ()
      {
         assert (m_active);
         return *m_upgradeLock_p;
      }

      VarMap& WTransactionData::GetGot ()
      {
         assert (m_active);
         return m_got;
      }
      
      VarMap& WTransactionData::GetSet ()
      {
         assert (m_active);
         return m_set;
      }      

      Internal::WLocalValueBase* WTransactionData::GetLocalValue (uint64_t key)
      {
         assert (m_active);
         
         WTransactionData* data_p = this;
         while (data_p)
         {
            const auto it = data_p->m_locals.find (key);
            if (it != data_p->m_locals.end ())
            {
               return it->second.get ();
            }
            data_p = data_p->m_parent_p;
         }
         
         return nullptr;         
      }
      
      void WTransactionData::SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p)
      {
         assert (m_active);
         m_locals[key] = std::move (value_p);
      }

      void WTransactionData::AddBeforeCommit (WAtomic::WBeforeCommitFunc& beforeCommit)
      {
         assert (m_active);
         m_beforeCommits.push_back (beforeCommit);
      }

      void WTransactionData::GetBeforeCommits (WBeforeCommitList& beforeCommits)
      {
         assert (m_active);
         beforeCommits.swap (m_beforeCommits);
      }

      void WTransactionData::AddAfter (WAtomic::WAfterFunc& after)
      {
         assert (m_active);
         m_afters.push_back (after);
      }

      void WTransactionData::GetAfters (WAfterList& afters)
      {
         assert (m_active);
         afters.swap (m_afters);
      }

      void WTransactionData::AddOnFail (WAtomic::WOnFailFunc& onFail)
      {
         assert (m_active);
         m_onFails.push_back (onFail);
      }

      void WTransactionData::RunOnFails ()
      {
         if (m_active)
         {
            BOOST_FOREACH (const WAtomic::WOnFailFunc& func, m_onFails)
            {
               func ();
            }
         }
         m_onFails.clear ();
      }

      void WTransactionData::MergeToParent ()
      {
         assert (m_active);
         assert (m_parent_p);

         //we can't use unordered_map::insert here because it won't
         //update elements that are already in the parent transaction
         BOOST_FOREACH (VarMap::value_type& value, m_got)
         {
            VarMap::iterator it;
            bool inserted = false;
            boost::tie (it, inserted) = m_parent_p->m_got.insert (value);
            if (!inserted)
            {
               it->second = value.second;
            }
         }
         BOOST_FOREACH (VarMap::value_type& value, m_set)
         {
            VarMap::iterator it;
            bool inserted = false;
            boost::tie (it, inserted) = m_parent_p->m_set.insert (value);
            if (!inserted)
            {
               it->second = value.second;
            }
         }

         BOOST_FOREACH (auto& val, m_locals)
         {
            m_parent_p->m_locals[val.first] = std::move (val.second);
         }

         m_parent_p->m_beforeCommits.splice (m_parent_p->m_beforeCommits.end (), m_beforeCommits);
         m_parent_p->m_afters.splice (m_parent_p->m_afters.end (), m_afters);
         m_parent_p->m_onFails.splice (m_parent_p->m_onFails.end (), m_onFails);
         
         Clear ();
      }

      void WTransactionData::MergeGetsToRoot ()
      {
         assert (m_active);
         assert (m_parent_p);

         WTransactionData* root_p = m_parent_p;
         while (root_p->m_parent_p)
         {
            root_p = root_p->m_parent_p;
         }
         
         BOOST_FOREACH (VarMap::value_type& value, m_got)
         {
            VarMap::iterator it;
            bool inserted = false;
            boost::tie (it, inserted) = m_parent_p->m_got.insert (value);
            if (!inserted)
            {
               it->second = value.second;
            }
         }

         Clear ();
      }
      
      void WTransactionData::Clear ()
      {
         if (!m_got.empty ())
         {
            m_got.clear ();
         }
         ClearWrites ();
         if (!m_onFails.empty ())
         {
            m_onFails.clear ();
         }
         m_active = false;
      }      

      void WTransactionData::ClearWrites()
      {
         if (!m_set.empty ())
         {
            m_set.clear ();
         }

         //For some reason we are getting random crashes when clearing
         //the after list when the list is empty so only clear it when it
         //is non-empty. 
         if (!m_beforeCommits.empty ())
         {
            m_beforeCommits.clear();
         }

         if (!m_afters.empty ())
         {
            m_afters.clear();
         }

         if (!m_locals.empty ())
         {
            m_locals.clear ();
         }

         if (!m_upgradeLock_p->locked ())
         {
            m_readLock.UnlockAll ();
         }
         else if (m_level == 1)
         {
            m_upgradeLock_p->UnlockAll ();
         }
      }

      void* WTransactionData::GetMarker () const
      {
         return m_marker;
      }
   }
   

   namespace 
   {
      //Stores the transaction data for each thread
#ifdef WIN32
      //Going directly to Win32 API here since
      //boost::thread_specific_ptr has performance issues
      class WTransactionDataTSS
      {
      public:
         WTransactionDataTSS ();

         Internal::WTransactionData* Get ();
         Internal::WTransactionData* GetNew ();

         class WPushGuard
         {
         public:
            WPushGuard (Internal::WTransactionData* data_p);
            WPushGuard (WPushGuard&& g);
            ~WPushGuard ();
            
         private:
            Internal::WTransactionData* m_data_p;
         };
         friend WPushGuard;
         WPushGuard Push ();
         
         void MergeToParent ();
         void Abandon ();

         void CheckIntegrity () const;
         
      private:
         static void AtThreadExit (const DWORD tlsIndex);

         Internal::WTransactionData* GetNewNoActivate ();

         DWORD m_tlsIndex;
      };

      WTransactionDataTSS s_transData_p;

      struct WOutOfTLSIndexes : public WException
      {
         WOutOfTLSIndexes ();
      };

      WOutOfTLSIndexes::WOutOfTLSIndexes ():
         WException ("Out of TLS indexes")
      {}

      WTransactionDataTSS::WTransactionDataTSS ()
      {
         m_tlsIndex = TlsAlloc ();
         if (m_tlsIndex == TLS_OUT_OF_INDEXES)
         {
            //If we get here then the program is already generating a
            //ton of TLS indexes and we can't get one so throw an
            //exception which will terminate the program.
            throw WOutOfTLSIndexes ();
         }
      }

      struct WTlsSetValueError : public WException
      {
         WTlsSetValueError (const DWORD errCode);

         DWORD m_errCode;
      };

      WTlsSetValueError::WTlsSetValueError (const DWORD errCode):
         WException (str (format ("Got TLS set error: %1%") % errCode)),
         m_errCode (errCode)
      {}

      Internal::WTransactionData* WTransactionDataTSS::Get ()
      {
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         if (data_p)
         {
            return data_p;
         }
         else
         {
            return GetNewNoActivate ();
         }
      }

      Internal::WTransactionData* WTransactionDataTSS::GetNew ()
      {
         Internal::WTransactionData* data_p = GetNewNoActivate ();
         data_p->Activate();
         return data_p;
      }

      Internal::WTransactionData* WTransactionDataTSS::GetNewNoActivate ()
      {
         CheckIntegrity ();
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         if (!data_p)
         {
            data_p = stm_new Internal::WTransactionData;
            LOG_MEMORY_ALLOCATION (WTransactionData, data_p);
            boost::this_thread::at_thread_exit (
               boost::bind (WTransactionDataTSS::AtThreadExit, m_tlsIndex));
            TrackThreadInit ();
         }
         else if (!data_p->IsActive ())
         {
            CheckThreadInited ();
            //skip setting the TLS since it already contains the
            //correct pointer
            return data_p;
         }
         else
         {
#ifdef TRACK_INITED_THREADS
            std::vector<DWORD>& inited = s_initedThreads;
#endif
            CheckThreadInited ();
            Internal::WTransactionData* child_p = data_p->GetChild ();
            if (child_p)
            {
               assert (!child_p->IsActive ());
               data_p = child_p;
            }
            else
            {                
               data_p = stm_new Internal::WTransactionData (data_p);            
               LOG_MEMORY_ALLOCATION (WTransactionData, data_p);
            }
         }

         const BOOL res = TlsSetValue (m_tlsIndex, data_p);
         if (!res)
         {
            //This shouldn't happen as long as TlsAlloc succeeded
            //above (and if that fails we shouldn't get this far),
            //but if we do get here there's no way to recover so
            //throw an exception that the caller can't catch in
            //order to terminate the program and generate a dump
            //file (which should capture the error code).
            const DWORD errCode = GetLastError ();
            throw WTlsSetValueError (errCode);
         }
         CheckIntegrity ();
         
         return data_p;
      }

      WTransactionDataTSS::WPushGuard::WPushGuard (Internal::WTransactionData* data_p):
         m_data_p (data_p)
      {}
      
      WTransactionDataTSS::WPushGuard::WPushGuard (WPushGuard&& g):
         m_data_p (g.m_data_p)
      {
         g.m_data_p = nullptr;
      }
      
      WTransactionDataTSS::WPushGuard::~WPushGuard ()
      {
         if (m_data_p)
         {
            WTransactionDataTSS::AtThreadExit (s_transData_p.m_tlsIndex);
            const BOOL res = TlsSetValue (s_transData_p.m_tlsIndex, m_data_p);
            if (!res)
            {
               //This shouldn't happen as long as TlsAlloc succeeded
               //above (and if that fails we shouldn't get this far),
               //but if we do get here there's no way to recover so
               //throw an exception that the caller can't catch in
               //order to terminate the program and generate a dump
               //file (which should capture the error code).
               const DWORD errCode = GetLastError ();
               throw WTlsSetValueError (errCode);
            }
         }
      }

      WTransactionDataTSS::WPushGuard WTransactionDataTSS::Push ()
      {
         CheckIntegrity ();
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         if (data_p)
         {
            const BOOL res = TlsSetValue (m_tlsIndex, nullptr);
            if (!res)
            {
               //This shouldn't happen as long as TlsAlloc succeeded
               //above (and if that fails we shouldn't get this far),
               //but if we do get here there's no way to recover so
               //throw an exception that the caller can't catch in
               //order to terminate the program and generate a dump
               //file (which should capture the error code).
               const DWORD errCode = GetLastError ();
               throw WTlsSetValueError (errCode);
            }
         }
         return WPushGuard (data_p);
      }

      void WTransactionDataTSS::MergeToParent ()
      {
         CheckIntegrity ();
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         assert (data_p->IsActive ());
         assert (data_p->GetParent ());
         if (data_p->GetParent ())
         {
            data_p->MergeToParent ();
            Internal::WTransactionData* parent_p = data_p->GetParent ();
            const BOOL res = TlsSetValue (m_tlsIndex, parent_p);
            if (!res)
            {
               //This shouldn't happen as long as TlsAlloc succeeded
               //above (and if that fails we shouldn't get this far),
               //but if we do get here there's no way to recover so
               //throw an exception that the caller can't catch in
               //order to terminate the program and generate a dump
               //file (which should capture the error code).
               const DWORD errCode = GetLastError ();
               throw WTlsSetValueError (errCode);
            }
            CheckIntegrity ();
         }
      }

      void WTransactionDataTSS::Abandon ()
      {
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         assert (data_p);
         if (!data_p)
         {
            return;
         }
         data_p->Clear ();
         
         if (data_p->GetParent ())
         {
            Internal::WTransactionData* parent_p = data_p->GetParent ();
            const BOOL res = TlsSetValue (m_tlsIndex, parent_p);
            if (!res)
            {
               //This shouldn't happen as long as TlsAlloc succeeded
               //above (and if that fails we shouldn't get this far),
               //but if we do get here there's no way to recover so
               //throw an exception that the caller can't catch in
               //order to terminate the program and generate a dump
               //file (which should capture the error code).
               const DWORD errCode = GetLastError ();
               throw WTlsSetValueError (errCode);
            }
            CheckIntegrity ();
         }         
      }
      
      void WTransactionDataTSS::AtThreadExit (const DWORD tlsIndex)
      {
         //first find the root transaction
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (tlsIndex));
         if (!data_p)
         {
            return;
         }
         Internal::WTransactionData* parent_p = data_p->GetParent ();
         while (parent_p)
         {
            data_p = parent_p;
            parent_p = data_p->GetParent ();
         }

         //now delete the root followed by each of its children
         while (data_p)
         {
            Internal::WTransactionData* child_p = data_p->GetChild ();
            LOG_MEMORY_DEALLOCATION (WTransactionData, data_p);
            delete data_p;
            data_p = child_p;
         }

         //set the TLS to zero so that if a transaction is started
         //after this function runs a new data object will be created
         TlsSetValue (tlsIndex, 0);
         
         TrackThreadExit ();
      }

//#define CHECK_TLS_INTEGRITY
#ifdef CHECK_TLS_INTEGRITY

      void WTransactionDataTSS::CheckIntegrity () const
      {
         Internal::WTransactionData* data_p =
            static_cast<Internal::WTransactionData*>(TlsGetValue (m_tlsIndex));
         if (data_p)
         {            
            Internal::WTransactionData* cur_p = data_p;
            Internal::WTransactionData* parent_p = cur_p->GetParent ();
            assert (cur_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
            
            while (parent_p)
            {
               assert (parent_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
               assert (parent_p->GetChild () == cur_p);
               assert (parent_p->GetLevel () == (cur_p->GetLevel () - 1));
               cur_p = parent_p;
               parent_p = cur_p->GetParent ();
            }

            cur_p = data_p;
            Internal::WTransactionData* child_p = cur_p->GetChild ();
            while (child_p)
            {
               assert (child_p->GetMarker () == Internal::WTransactionData::MARKER_VALUE);
               assert (child_p->GetParent () == cur_p);
               assert (child_p->GetLevel () == (cur_p->GetLevel () + 1));
               cur_p = child_p;
               child_p = cur_p->GetChild ();
            }
         }
      }
      
#else
      void WTransactionDataTSS::CheckIntegrity () const
      {}
#endif CHECK_TLS_INTEGRITY

#endif //WIN32      
   }

   namespace Internal
   {
      
#ifdef _DEBUG

      inline bool ReadBoolPtr(const boost::thread_specific_ptr<bool>& ptr)
      {
         const bool* locked_p = ptr.get();
         return locked_p ? *locked_p : false;
      }
      
      bool ReadLocked()
      {
         return ReadBoolPtr(s_readMutexReadLocked);
      }
      
      bool UpgradeLocked()
      {
         return ReadBoolPtr(s_readMutexUpgradeLocked);
      }
      
      bool WriteLocked()
      {
         return ReadBoolPtr(s_readMutexWriteLocked);
      }      
#endif //_DEBUG

      WValueBase::WValueBase (const size_t version):
         m_version (version)
      {}

      WValueBase::~WValueBase ()
      {
         LOG_MEMORY_DEALLOCATION (WValue, this);
      }

      WVarCoreBase::~WVarCoreBase ()
      {
         LOG_MEMORY_DEALLOCATION (WVarCore, this);
      }

   }
   
   WAtomic::WAtomic ():
      m_data_p (s_transData_p.GetNew ()),
      m_committed (false)
   {
#ifdef _DEBUG
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         assert (data_p->IsActive ());
         data_p = data_p->GetParent ();
      }
#endif //DEBUG
   }

   void WAtomic::Validate() const
   {
      boost::unique_lock<WReadLock> lock(m_data_p->GetReadLock (), boost::defer_lock_t ());
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         lock.lock ();
      }
      if(!DoValidation())
      {
         throw Internal::WFailedValidationException();
      }
   }

   bool WAtomic::DoValidation() const
   {
      assert(Internal::ReadLocked() || Internal::UpgradeLocked ());
      BOOST_FOREACH (const VarMap::value_type& val, m_data_p->GetGot ())
      {
         if (!val.first->Validate (*val.second))
         {
            return false;
         }
      }

      return true;
   }

   void WAtomic::ReadLock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetReadLock ().lock();
      }
   }

   bool WAtomic::IsReadLocked() const
   {
      return m_data_p->GetReadLock ().locked ();
   }

   void WAtomic::ReadUnlock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetReadLock ().unlock();
      }
   }

   void WAtomic::BeforeCommit (WBeforeCommitFunc func)
   {
      m_data_p->AddBeforeCommit (func);
   }

   void WAtomic::After(WAfterFunc func)
   {
      m_data_p->AddAfter (func);
   }

   void WAtomic::OnFail (WOnFailFunc func)
   {
      m_data_p->AddOnFail (func);
   }

   void WAtomic::ClearWrites()
   {
      m_data_p->ClearWrites ();
   }

   void WAtomic::Abort ()
   {
      m_data_p->Clear ();
   }

   void WAtomic::Restart ()
   {
      //We need to push our transaction data aside here, if we don't
      //and a destructor triggered by the clearing of data below
      //starts a new transaction it will end up being a child of the
      //transaction that is being restarted. If this happens at best
      //the actions of the detstructor will never be comitted, at
      //worst memory corruption will result.
      WTransactionDataTSS::WPushGuard guard = s_transData_p.Push ();
      m_data_p->Clear ();
      m_data_p->Activate ();
   }

   void WAtomic::RunOnFails ()
   {
      //We need to push our transaction data aside here so that
      //transactions in the "on fail" handlers will run properly
      //(properly here means: no memory corruption)
      WTransactionDataTSS::WPushGuard guard = s_transData_p.Push ();
      m_data_p->RunOnFails ();
   }

   void WAtomic::CommitLock()
   {
      if (!m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetUpgradeLock ().lock ();
         //unlock all the old read-locks here else we won't be able to
         //promote our upgrade lock later
         m_data_p->GetReadLock ().UnlockAll ();
      }
   }
   
   bool WAtomic::Commit()
   {
      assert (m_data_p->GetLevel () == 1);
      if(m_data_p->GetLevel () == 1)
      {
         Internal::WTransactionData::WBeforeCommitList beforeCommits;
         m_data_p->GetBeforeCommits (beforeCommits);         
         BOOST_FOREACH (WAtomic::WBeforeCommitFunc& beforeCommit, beforeCommits)
         {
            beforeCommit (*this);
         }
         
#ifdef _DEBUG
         bool* committing_p = s_committing.get();
         if(!committing_p)
         {
            committing_p = stm_new bool;
            s_committing.reset(committing_p);
         }
         *committing_p = true;
         struct WClearFlag
         {
            bool* m_committing_p;
            ~WClearFlag()
            {
               *m_committing_p = false;
            }
         };
         WClearFlag clearFlag = {committing_p};
         (void)clearFlag; //avoid a compiler warning
#endif _DEBUG
         
         std::list<Internal::WValueBase::Ptr> dead;
         if (!m_data_p->GetSet ().empty ())
         {
            CommitLock ();
            
            if(!DoValidation())
            {
               m_data_p->GetUpgradeLock ().UnlockAll ();
               return false;

            }
            {   
               //scope introduced so that wlock goes away at end of block
               WWriteLock wlock(m_data_p->GetUpgradeLock ());
               BOOST_FOREACH (const VarMap::value_type& val, m_data_p->GetSet ())
               {
                  //save old values until after we're done committing
                  //in case they run transactions in their destructors
                  dead.push_back (val.first->Commit (val.second));
               }
               s_commitSignal.notify_all();
            }

            m_data_p->GetUpgradeLock ().UnlockAll ();
            IncrementNumWriteCommits ();
         }
         else
         {
            if (m_data_p->GetUpgradeLock ().locked ())
            {
               if(!DoValidation())
               {
                  m_data_p->GetUpgradeLock ().UnlockAll ();
                  return false;
               }
               m_data_p->GetUpgradeLock ().UnlockAll ();
            }
            else
            {
               m_data_p->GetReadLock ().lock ();
               if(!DoValidation())
               {
                  m_data_p->GetReadLock ().UnlockAll ();
                  return false;
               }
               m_data_p->GetReadLock ().UnlockAll ();
            }
            IncrementNumReadCommits ();
         }

         //reset transaction data here so that after funcs will see no
         //transaction in progress         
         Internal::WTransactionData::WAfterList afters;
         m_data_p->GetAfters (afters);
         m_data_p->Clear ();
#ifdef _DEBUG
         *committing_p = false;
#endif //_DEBUG
         m_committed = true;

         //The dead must be cleared before the afters run, else we coudl be holding on to things
         //that the afters are relying on being gone already (specifically
         //WChannelReader::Wdata::Release assumes that the after function that it attaches won't be
         //fighting with the transaction to release the channel nodes in order to avoid a stack overflow). 
         dead.clear ();
         
         BOOST_FOREACH (WAtomic::WAfterFunc& after, afters)
         {
            after ();
         }

      }

      return true;
   }

   bool WAtomic::WaitForChanges(const WTimeArg& timeout)
   {
      if (m_data_p->GetUpgradeLock ().locked ())
      {
         m_data_p->GetUpgradeLock ().unlock ();
      }

      WReadLockGuard<WAtomic> rlock(*this);
      if(!timeout.IsUnlimited ())
      {
         do
         {
            if(!DoValidation())
            {
               return true;
            }
            m_data_p->GetReadLock ().WaitForCommit(&timeout);
         }while(boost::get_system_time() < timeout);
      }
      else
      {
         while(true)
         {
            if(!DoValidation())
            {
               return true;
            }
            m_data_p->GetReadLock ().WaitForCommit(0);
         }
      }

      return false;
   }

   const Internal::WValueBase* WAtomic::GetVarValue (const Internal::WVarCoreBase::Ptr& core_p)
   {
      //Look in the values of this transaction and its parents
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         //first try the set value
         VarMap::iterator it = data_p->GetSet ().find (core_p);
         if (it != data_p->GetSet ().end ())
         {
            return it->second.get ();
         }

         //next the got value
         it = data_p->GetGot ().find (core_p);
         if (it != data_p->GetGot ().end ())
         {
            return it->second.get ();
         }

         //try the parent transaction
         data_p = data_p->GetParent ();
      }

      //We haven't seen this var yet
      return nullptr;
   }

   const Internal::WValueBase* WAtomic::GetVarGotValue (const Internal::WVarCoreBase::Ptr& core_p)
   {
      //Look in the values of this transaction and its parents
      Internal::WTransactionData* data_p = m_data_p;
      while (data_p)
      {
         //next the got value
         auto it = data_p->GetGot ().find (core_p);
         if (it != data_p->GetGot ().end ())
         {
            return it->second.get ();
         }

         //try the parent transaction
         data_p = data_p->GetParent ();
      }

      //We haven't seen this var yet
      return nullptr;      
   }

   void WAtomic::SetVarGetValue (const Internal::WVarCoreBase::Ptr& core_p,
                                 const Internal::WValueBase::Ptr& value_p)
   {
      m_data_p->GetGot ().insert (std::make_pair (core_p, value_p));
   }

   Internal::WValueBase* WAtomic::GetVarSetValue (const Internal::WVarCoreBase::Ptr& core_p)
   {
      //Note that we only check this transaction's set values not the
      //parent's. Values need to be set in the current transaction,
      //even if they have already been set in the parent. When this
      //transaction commits the set values will be merged into the
      //parent transaction.
      VarMap::iterator it = m_data_p->GetSet ().find (core_p);
      if (it != m_data_p->GetSet ().end ())
      {
         return it->second.get ();
      }
      else
      {
         return 0;
      }
   }
   
   void WAtomic::SetVarValue (const Internal::WVarCoreBase::Ptr& core_p,
                              const Internal::WValueBase::Ptr& value_p)
   {
      VarMap::iterator it;
      bool inserted = false;
      boost::tie (it, inserted) = m_data_p->GetSet ().insert (std::make_pair (core_p, value_p));
      if (!inserted)
      {
         it->second = value_p;
      }
   }

   Internal::WLocalValueBase* WAtomic::GetLocalValue (uint64_t key)
   {
      return m_data_p->GetLocalValue (key);
   }

   void WAtomic::SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p)
   {
      m_data_p->SetLocalValue (key, std::move (value_p));
   }
   
   WAtomic::~WAtomic()
   {
      if (!m_committed)
      {
         s_transData_p.Abandon ();
      }
   }

   namespace
   {
#define TRACK_LAST_TRANS_CONFLICTS
#ifdef TRACK_LAST_TRANS_CONFLICTS
      void DoNothing (int*)
      {}
      
      boost::thread_specific_ptr<int> s_lastTransConflicts (DoNothing);

      struct WSetLastTransConflicts
      {
         const unsigned int& m_badCommits;

         WSetLastTransConflicts (const unsigned int& badCommits);

         ~WSetLastTransConflicts ();
      };

      WSetLastTransConflicts::WSetLastTransConflicts (const unsigned int& badCommits):
         m_badCommits (badCommits)
      {}

      WSetLastTransConflicts::~WSetLastTransConflicts ()
      {
         s_lastTransConflicts.reset ((int*)m_badCommits);
      }
#endif //TRACK_LAST_TRANS_CONFLICTS

   }

   void WAtomic::AtomicallyImpl(WAtomicOp op, const WAtomicallyArgs& args)
   {      
#ifdef _DEBUG
      //if this assertion fails we got a new transaction starting
      //while the current transaction is committing
      bool* committing_p = s_committing.get();
      assert(!committing_p || !*committing_p);
#endif _DEBUG

      WAtomic at;
      assert (!at.m_committed);
      struct WRunOnFailHandlers
      {
         WAtomic& m_at;

         WRunOnFailHandlers (WAtomic& at): m_at (at) {}

         ~WRunOnFailHandlers ()
         {
            if (!m_at.m_committed)
            {
               m_at.RunOnFails ();
            }
         }
      };
      WRunOnFailHandlers runOnFailHandlers (at);
      if (at.m_data_p->GetLevel () > 1)
      {
         try
         {
            //this is a child transaction so just run the op and
            //merge to parent, exceptions and committing will be handled by the
            //root transaction
            op (at);
            s_transData_p.MergeToParent ();
            at.m_committed = true;
            return;
         }
         catch(WRetryException&)
         {
            at.RunOnFails ();
            //we need to merge our gets into the root transaction so
            //that those vars get checked in the root transaction
            //retry handler
            at.m_data_p->MergeGetsToRoot ();
            throw;
         }
      }

      unsigned int badCommits = 0;
#ifdef TRACK_LAST_TRANS_CONFLICTS
      const size_t numConflictsLastTime = (size_t)s_lastTransConflicts.get ();
      (void)numConflictsLastTime;
      WSetLastTransConflicts setLastTransConflicts (badCommits);
#endif //TRACK_LAST_TRANS_CONFLICTS
      unsigned int retries = 0;
      while(true)
      {
         if(args.m_maxConflicts != UNLIMITED && badCommits >= args.m_maxConflicts)
         {
            if(WConflictResolution::THROW == args.m_conRes)
            {
               throw WMaxConflictsException(badCommits);
            }
            else
            {
               LOG_ERROR (runLockedLogger, "RUNNING LOCKED");
               at.CommitLock();
            }
         }
         
         try
         {
            op(at);
         }
         catch(Internal::WFailedValidationException&)
         {
            ++badCommits;
            IncrementNumConflicts ();
            at.RunOnFails ();
            at.Restart();
            continue;
         }
         catch(WRetryException& exc)
         {
            ++retries;
            if(args.m_maxRetries != UNLIMITED && retries >= args.m_maxRetries)
            {
               throw WMaxRetriesException(retries);
            }
            at.RunOnFails ();

            WTimeArg timeout = exc.m_timeout;
            if (args.m_maxRetryWait < timeout)
            {
               timeout = args.m_maxRetryWait;
            }
            if(!at.WaitForChanges(timeout))
            {
               throw WRetryTimeoutException();
            }
            at.Restart();
            continue;
         }
         catch (...)
         {
            at.RunOnFails ();
            at.Restart();
            throw;
         }

         if(!at.Commit())
         {
            at.RunOnFails ();
            at.Restart();
            ++badCommits;
            IncrementNumConflicts ();
            continue;
         }
         break;

      }
   }

   void WInconsistent::InconsistentlyImpl(WInconsistentOp op)
   {
      WInconsistent ins;
      op(ins);
   }

   WInconsistent::WInconsistent() :
      m_lock (s_readMutex, boost::defer_lock_t ()),
      m_lockCount (0)
   {}

   void WInconsistent::ReadLock()
   {
      if (m_lockCount == 0)
      {
         m_lock.lock();
      }
      ++m_lockCount;
   }
   
   bool WInconsistent::IsReadLocked() const
   {
      return m_lock.owns_lock();
   }
   
   void WInconsistent::ReadUnlock()
   {
      if(m_lockCount > 0)
      {
         --m_lockCount;
         if (m_lockCount == 0)
         {
            m_lock.unlock();
         }
      }
   }

   bool InAtomic()
   {
      return (s_transData_p.Get()->IsActive ());
   }

   WNoAtomic::WNoAtomic ()
   {
      if (InAtomic ())
      {
         throw WInAtomicError ();
      }
   }

   WInAtomicError::WInAtomicError ():
      WException ("Attempt to use function marked NO ATOMIC from within a transaction")
   {}
   
   void Retry(WAtomic&, const WTimeArg& timeout)
   {
      throw WRetryException(timeout);
   }

   bool WTransactionLocalFlag::TestAndSet (WAtomic& at)
   {
      const auto isSet = m_flag.Get (at) && *m_flag.Get (at);
      m_flag.Set (true, at);
      return isSet;
   }

}}}
