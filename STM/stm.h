/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2009. All rights reserved.
****************************************************************************/

#pragma once

#ifdef WIN32
#pragma warning (push)
#pragma warning (disable: 4127 4239 4244 4251 4265 4275 4503 4505 4512 4640 4996 6011)
#endif

#include "BSS/wtcbss.h"
#include "BSS/Common/Pointers.h"
#include "BSS/Common/CombinatorArgs.h"
#include "BSS/Common/TimeArg.h"
#include "BSS/Common/NotCopyable.h"

#include <boost/shared_ptr.hpp>
#include <boost/any.hpp>
#include <boost/variant.hpp>
#include <boost/format.hpp>
#include <boost/function.hpp>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_convertible.hpp>
#include <boost/type_traits/is_same.hpp>
#include <boost/type_traits/is_void.hpp>
#include <boost/type_traits/remove_const.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/functional.hpp>
#include <boost/mpl/if.hpp>
#include <boost/call_traits.hpp>
#include <boost/utility.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/tss.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/utility/declval.hpp>

#include <string>
#include <list>
#include <vector>
#include <deque>
#include <set>
#include <utility>

#undef max
#include <limits>

#ifdef WIN32
#ifdef _DEBUG
#define stm_new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#else
#define stm_new new
#endif
#else
#define stm_new new
#endif

namespace bss { namespace thread { namespace STM
{
   /**
    * Starts a profiling run. Note that STM_PROFILING must be defined
    * in stm.cpp for this to do anything.
    */
   void BSS_LIBAPI StartProfiling ();

   /**
    * Data from a STM profile run. Pass these objects to Checkpoint.
    */
   struct BSS_CLASSAPI WProfileData
   {
      //!The start time of the profile run.
      boost::posix_time::ptime m_start;
      //!The end time of the profile run.
      boost::posix_time::ptime m_end;
      
      //!The number of conflicts during the run.
      long m_numConflicts;
      //!The number of read-only commits during the run.
      long m_numReadCommits;
      //!The number of commits with writes during the run.
      long m_numWriteCommits;

      //!Formats the data for output.
      std::string FormatData () const;
   };
   
   /**
    * Ends a profiling run. Note that STM_PROFILING must be defined
    * in stm.cpp for this to do anything.
    *  
    * @return The profile data for this run.
    */
   WProfileData BSS_LIBAPI Checkpoint ();
   
   /**
      Constant that denotes an unlimited number of tries.
   */
   BSS_LIBAPI extern const unsigned int UNLIMITED;

   class WAtomic;
   typedef boost::function<void (WAtomic&)> WAtomicOp;
   class WInconsistent;
   typedef boost::function<void (WInconsistent&)> WInconsistentOp;
	
   /**
      Enumeration that tells STM::atomically how to react when it
      reaches it's conflict limit.
   */
   struct BSS_CLASSAPI WConflictResolution
   {
      enum Value
      {
         /**
            A MaxConflictsException will be thrown.
         */
         THROW,
         /**
            The operation will be run with all other
            writes locked out thus guaranteeing that the
            operation can complete successfully
         */
         RUN_LOCKED	
      };
   };

   namespace Internal
   {
#ifdef _DEBUG
      bool BSS_LIBAPI ReadLocked();
      bool BSS_LIBAPI UpgradeLocked();
      bool BSS_LIBAPI WriteLocked();
#endif //_DEBUG

      void BSS_LIBAPI LogMemoryAllocation (const char* typeName,
                                           const void* address,
                                           const char* filename,
                                           const int line);
      void BSS_LIBAPI LogMemoryDeallocation (const char* typeName,
                                             const void* address,
                                             const char* filename,
                                             const int line);
#define LOG_MEMORY_ALLOCATION(typeName, address)                    \
      LogMemoryAllocation (#typeName, address, __FILE__, __LINE__)//
#define LOG_MEMORY_DEALLOCATION(typeName, address)                     \
      LogMemoryDeallocation (#typeName, address, __FILE__, __LINE__)//
      
      struct WTransactionData;

      //Thrown by WVarCoreBase::Validate when validation fails
      struct BSS_CLASSAPI WFailedValidationException
      {};

      struct BSS_CLASSAPI WValueBase
      {
         DEFINE_POINTERS (WValueBase);
         size_t m_version;

         WValueBase (const size_t version);
         ~WValueBase ();
      };

      template <typename Type_t>
      struct WValue : public WValueBase
      {
         DEFINE_POINTERS (WValue);
         
         Type_t m_value;

         WValue (const size_t version, const Type_t& value);
      };

      template <typename Type_t>
      WValue<Type_t>::WValue (const size_t version, const Type_t& value):
         WValueBase (version),
         m_value (value)
      {}

      struct BSS_CLASSAPI WVarCoreBase
      {
         DEFINE_POINTERS (WVarCoreBase);
         
         virtual bool Validate (const WValueBase& val) const = 0;
         virtual WValueBase::Ptr Commit (const WValueBase::Ptr& val_p) = 0;
         virtual ~WVarCoreBase ();
      };

      template <typename Type_t>
      struct WVarCore : public WVarCoreBase
      {
         DEFINE_POINTERS (WVarCore);
         
         virtual bool Validate (const WValueBase& val) const;
         virtual WValueBase::Ptr Commit (const WValueBase::Ptr& val_p);
         
         explicit WVarCore(WValue<Type_t>* val_p);

         typename WValue<Type_t>::Ptr m_value_p;
      };

      template<typename Type_t>
      bool WVarCore<Type_t>::Validate (const WValueBase& val) const
      {
         return (val.m_version == m_value_p->m_version);
      }
      
      template<typename Type_t>
      WValueBase::Ptr WVarCore<Type_t>::Commit (const WValueBase::Ptr& val_p)
      {
         WValueBase::Ptr old_p = m_value_p;
#ifdef _DEBUG
         const auto oldPtr_p = static_cast<const WValue<Type_t>*>(old_p.get ());
         (void) oldPtr_p;
#endif
         m_value_p = boost::static_pointer_cast<WValue<Type_t> >(val_p);
         return old_p;
      }
         
      template<typename Type_t>
      WVarCore<Type_t>::WVarCore(WValue<Type_t>* val_p):
         m_value_p (val_p)
      {}
      
      // void BSS_LIBAPI RunOrElse(WAtomicOp op1, WAtomicOp op2, WAtomic& at);

      struct BSS_CLASSAPI WLocalValueBase
      {
      public:
         virtual ~WLocalValueBase ();
      };

      uint64_t BSS_LIBAPI GetTransactionLocalKey ();
   }

   /**
      Read lock manager for Atomic and Inconsistent. When the
      lock is required just create one of these objects and
      the lock will be released when this object goes out of
      scope.

      @param ReadLockable_t The type that will be read
      locked. Must have the methods readLock() and
      readUnlock().
   */
   template <typename ReadLockable_t>
   class WReadLockGuard
   {
   public:
      /**
         Creates a guard and read locks the given object.
      */
      WReadLockGuard(ReadLockable_t& lockable):
         m_lockable(lockable)
      {
         lockable.ReadLock();
      }

      /**
         Unlocks the object that was passed to the
         constructor.
      */
      ~WReadLockGuard()
      {
         m_lockable.ReadUnlock();
      }
				
   private:
      ReadLockable_t& m_lockable;
   };

   /**
    * Argument object for Atomically(). Normally this isn't passed
    * directly but is instead manipulated using WMaxConflicts,
    * WConRes, WMaxRetries and WMaxRetryWait.
    */
   struct BSS_CLASSAPI WAtomicallyArgs
   {
      unsigned int m_maxConflicts;
      WConflictResolution::Value m_conRes;
      unsigned int m_maxRetries;
      WTimeArg m_maxRetryWait;

      WAtomicallyArgs();
   };

   /**
    * Sets the the maximum number of times that the operation should
    * be re-run due to STM::WVar changes by other threads before
    * "conflict resolution" is used.  Pass STM::UNLIMITED to have no
    * limit (this is the default).
    */
   struct BSS_CLASSAPI WMaxConflicts : public CombinatorArgs::WArg<WAtomicallyArgs, WMaxConflicts>
   {
      WMaxConflicts(const unsigned int m) : m_max(m) {}
      unsigned int m_max;

      void apply(WAtomicallyArgs& a) const
      {
         a.m_maxConflicts = m_max;
      }
   };

   /**
    * How to handle things when the limit set by WMaxConflicts is
    * reached.  See WConflictResolution for more info.
    */
   struct BSS_CLASSAPI WConRes : public CombinatorArgs::WArg<WAtomicallyArgs, WConRes>
   {
      WConRes(const WConflictResolution::Value r) : m_res(r) {}
      WConflictResolution::Value m_res;

      void apply(WAtomicallyArgs& a) const
      {
         a.m_conRes = m_res;
      }
   };

   /**
    * The maximum number of times that STM::retry() can be called by
    * op or any functions that op calls.  If this limit is hit a
    * WMaxRetriesException will be thrown. Pass STM::UNLIMITED to have
    * no limit (this is the default).
    */
   struct BSS_CLASSAPI WMaxRetries : public CombinatorArgs::WArg<WAtomicallyArgs, WMaxRetries>
   {
      WMaxRetries(const unsigned int m) : m_max(m) {}
      unsigned int m_max;

      void apply(WAtomicallyArgs& a) const
      {
         a.m_maxRetries = m_max;
      }
   };

   /**
    * The maximum amount of milliseconds to wait for a retry (see
    * STM::Retry() for details).  If this limit is hit a
    * WRetryTimeoutException will be thrown.  Pass STM::UNLIMITED to
    * have no time limit (this is the default).
    */
   struct BSS_CLASSAPI WMaxRetryWait : public CombinatorArgs::WArg<WAtomicallyArgs, WMaxRetryWait>
   {
      WMaxRetryWait(const WTimeArg& timeout) : m_max(timeout) {}
      WTimeArg m_max;

      void apply(WAtomicallyArgs& a) const
      {
         a.m_maxRetryWait = m_max;
      }
   };

   /**
      Functions passed to STM::atomically must take a reference
      to one of these objects as their only argument.  The
      public interface allows one to do transaction
      validation and connect to a signal that is emitted when
      the transaction commits.
   */
   class BSS_CLASSAPI WAtomic
   {
      template <typename> friend class WVar;
      template <typename> friend class WTransactionLocalValue;
      //friend void Internal::RunOrElse(WAtomicOp op1, WAtomicOp op2, WAtomic& at);
   public:
      /**
         Checks the current transaction for memory
         consistency.  If any of the STM::Var's that have
         been read have been changed by another thread then
         the current transaction is aborted and restarted.
         It is not a requirement to call this, it is done
         automatically when the operation being run by
         STM::atomically completes.  It should be called
         periodically by long-running operations in order to
         avoid doing extra work when a transaction is
         already invalid.
      */
      void Validate() const;
				
      /**
         Causes the transaction to acquire a read
         lock. Normally during the course of reading a Var a
         read lock is acquired and released. This can be a
         performance problem when a lot of Vars need to be
         read. In those cases this method should be called
         so that one read lock will be held while doing the
         reads. The lock will be held until either
         readUnlock() has been called an equal number of
         times as readLock() or the transaction ends.  
      */
      void ReadLock();
      /**
         Returns true if the transaction is holding a read
         lock, false otherwise.
      */
      bool IsReadLocked() const;
      /**
         Releases the read lock acquired by a call to
         readLock(). This method must be called an equal
         number of times as readLock has been called in
         order for the lock to actually be released.
      */
      void ReadUnlock();

      //@{
      /**
         Adds a function to call just before the top-level transaction
         that is currently running starts to commit.

         @param func The function to call, it will be passed this
         transaction. Be careful about what you do in these
         functions. The function will not run until the TOP-LEVEL
         transaction commits.  This may be much later than you expect.
         Take steps to make sure that the data you think will be
         around when the function runs will still be around by using
         shared_ptr or something similar.
      */
      typedef boost::function<void (WAtomic&)> WBeforeCommitFunc;
      void BeforeCommit (WBeforeCommitFunc func);
      //@}

      //@{
      /**
         Adds a function to call after the top-level
         transaction that is currently running commits
         successfully.

         @param func The function to call after the
         top-level transaction commits. Be careful about
         what you do in these functions.  The function will
         not run until the TOP-LEVEL transaction commits.
         This may be much later than you expect.  Take steps
         to make sure that the data you think will be around
         when the function runs will still be around by using
         shared_ptr or something similar.
      */
      typedef boost::function<void (void)> WAfterFunc;
      void After(WAfterFunc func);
      //@}

      //@{
      /**
       * Adds a function that will be called if this transaction fails
       * to commit for some reason (e.g. there is a conflict, an
       * exception is thrown or the transacation is retried). This is
       * useufl if you allocate resources that need to be cleaned up
       * if the transaction fails to commit.
       *
       * @param func The function to call.
       */
      typedef boost::function<void (void)> WOnFailFunc;
      void OnFail (WOnFailFunc func);
      //@}
      
      //@{
      /**
         This method is used internally, just ignore it. You
         should be looking at STM::atomically() instead.
      */
      static void AtomicallyImpl(WAtomicOp op, const WAtomicallyArgs& args);

      //@}

      ~WAtomic();
				
   private:		
      WAtomic();

      WAtomic (const WAtomic&);
      WAtomic& operator=(const WAtomic&);
      
      //does validation, expects a read lock to already be
      //in effect. Returns true if transaction is valid false
      //otherwise.
      bool DoValidation() const;
      //Starts a commit lock that will be in effect until
      //the transaction is committed or aborted.
      void CommitLock();
      //commits the transaction. Returns true if the
      //transaction committed successfully, false if there
      //is an invalid Var read at this level or throws
      //FailedValidationException if there is an invalid var
      //read at a higher level. 
      bool Commit();
      //Forgets any sets done in this transaction. Also
      //drops any after functions and releases the read
      //lock. This is called as part of abort but the OR
      //stuff needs to be able to clear out everything
      //except the gets.
      void ClearWrites();
      //aborts the transaction. 
      void Abort();
      //Clears the transaction and gets it ready to run again
      void Restart ();
      //Runs the "on fail" handlers.
      void RunOnFails ();
      //waits for one of the Vars read by this transaction
      //to change. 
      bool WaitForChanges(const WTimeArg& timeout);

      //Gets the value for the given WVar, this will be null if a
      //value has not been "gotten" or "set" for this WVar in this transaction.
      const Internal::WValueBase* GetVarValue (const Internal::WVarCoreBase::Ptr& core_p);
      //Gets the value that has been "gotten" for the given WVar, this will be null if a value has
      //not been "gotten" for the WVar in this transaction. 
      const Internal::WValueBase* GetVarGotValue (const Internal::WVarCoreBase::Ptr& core_p);
      //Sets the "gotten" value for the given WVar in this transaction.
      void  SetVarGetValue (const Internal::WVarCoreBase::Ptr& core_p,
                            const Internal::WValueBase::Ptr& value_p);
      //Gets the value that has been set for the WVar, or null if no
      //value has been set.
      Internal::WValueBase* GetVarSetValue (const Internal::WVarCoreBase::Ptr& core_p);
      //Sets the given WVar's value in the transaction. 
      void SetVarValue (const Internal::WVarCoreBase::Ptr& core_p,
                        const Internal::WValueBase::Ptr& value_p);

      //Used by WTransactionLocalValue
      Internal::WLocalValueBase* GetLocalValue (uint64_t key);
      void SetLocalValue (uint64_t key, std::unique_ptr<Internal::WLocalValueBase>&& value_p);
      
      Internal::WTransactionData* m_data_p;
      bool m_committed;
   };
   
   /**
      Functions passed to STM::inconsistently must take a
      reference to one of these objects as their only
      argument.  Its only use is to read Var's.
   */
   class BSS_CLASSAPI WInconsistent
   {
   public:
      /**
         This is used internally, you want to look at
         inconsistently() instead.
      */
      static void InconsistentlyImpl(WInconsistentOp op);

      /**
         Causes a read lock to be acquired. Normally during
         the course of reading a Var a read lock is acquired
         and released. This can be a performance problem
         when a lot of Vars need to be read. In those cases
         this method should be called so that one read lock
         will be held while doing the reads. The lock will
         be held until either readUnlock() has been called
         an equal number of times as readLock() or the
         transaction ends.
      */
      void ReadLock();
      /**
         Returns true if the transaction is holding a read
         lock, false otherwise.
      */
      bool IsReadLocked() const;
				
      /**
         Releases the read lock acquired by a call to
         readLock(). This method must be called an equal
         number of times as readLock has been called in
         order for the lock to actually be released.
      */
      void ReadUnlock();
				
   private:
      WInconsistent();

      WInconsistent (const WInconsistent&);
      WInconsistent& operator= (const WInconsistent&);

      boost::shared_lock<boost::upgrade_mutex> m_lock;
      size_t m_lockCount;
   };

   namespace Internal
   {
      /*
       * We need our own result_of template because vs2010
       * std::result_of and boost 1.47 boost::result_of cannot handle
       * c++ lambda objects. This is probably due to vs2010 lacking
       * variadic templates (GCC can handle lambdas in result_of, but
       * it needs variadic templates to implement it).
       */
      template <typename Op_t>
      struct WAtomicOpResultType
      {
         typedef
         decltype (boost::declval<Op_t>() (boost::declval<WAtomic>()))
         type;
      };

      template <typename Op_t>
      struct WInconsistentOpResultType
      {
         typedef
         decltype (boost::declval<Op_t>() (boost::declval<WInconsistent>()))
         type;
      };

      template <typename Trans_t, typename Op_t, typename Result_t, bool IsRef>
      struct WValOp;

      template <typename Trans_t, typename Op_t, typename Result_t>
      struct WValOp <Trans_t, Op_t, Result_t, true>
      {
         typedef void result_type;
         typedef Trans_t& argument_type;
			
         WValOp(const Op_t& op): m_op(op ) {}

         void operator()(Trans_t& t)
         {
            m_res_p = &m_op(t);
         }

         Result_t GetResult()
         {
            return *m_res_p;
         }

         const Op_t& m_op;
         typedef typename std::remove_reference<typename std::remove_const<Result_t>::type>::type Res_t;
         Res_t* m_res_p;
  
      private:
         WValOp& operator= (const WValOp&) { return *this; }   // Silence warning 4512.
      };

      template <typename Trans_t, typename Op_t, typename Result_t>
      struct WValOp <Trans_t, Op_t, Result_t, false>
      {
         typedef void result_type;
         typedef Trans_t& argument_type;
         typedef typename boost::remove_const<Result_t>::type Res_t;
			
         WValOp(const Op_t& op): m_op(op ) {}

         void operator()(Trans_t& t)
         {
            m_res = m_op(t);
         }

         Result_t GetResult()
         {
            return std::move (boost::get<Res_t>(m_res));
         }

         //Using a variant to store either a WEmpty or the result. WEmpty is the default so that we
         //don't have to require Result_t to have a default constructor. We also move into the
         //variant so that we can handle "move-only" objects.
         struct WEmpty
         {};
         typedef boost::variant<WEmpty, Res_t> Result;
         
         const Op_t& m_op;
         Result m_res;
  
      private:
         WValOp& operator= (const WValOp&) { return *this; }   // Silence warning 4512.
      };
   }

   /**
    * Base class for all exceptions thrown by STM functions.
    */
   struct BSS_CLASSAPI WException
   {
      WException (const std::string& msg);
      const std::string m_msg;
  
   private:
      WException& operator= (const WException&) { return *this; }   // Silence warning 4512.
   };

   /**
      Base class for exceptions thrown by STM::atomically.
   */
   struct BSS_CLASSAPI WCantContinueException : public WException
   {
      WCantContinueException(const std::string& msg_);
   };

   /**
      Exception thrown when STM::atomically hits it's retry limit.
   */
   struct BSS_CLASSAPI WMaxRetriesException : public WCantContinueException
   {
      WMaxRetriesException(unsigned int retries):
         WCantContinueException(
            boost::str(boost::format("Hit maximum number of retries (%1%)") % retries))
      {}
   };

   /**
      Exception thrown when STM::atomically hits it's conflict limit.
   */
   struct BSS_CLASSAPI WMaxConflictsException : public WCantContinueException
   {
      WMaxConflictsException(unsigned int conflicts):
         WCantContinueException(
            boost::str(boost::format("Hit maximum number of conflicts (%1%)") % conflicts))
      {}
   };

   /**
      Exception thrown when a retry times out.
   */
   struct BSS_CLASSAPI WRetryTimeoutException : public WCantContinueException
   {
      WRetryTimeoutException():
         WCantContinueException("Retry timed out")
      {}
   };
   
   //!{
   /**
      Runs the given operation in an atomic fashion.  This
      means that when the operation runs any changes it makes
      to STM::WVar's are done in a transaction and will only
      be visible to other threads if the operation completes
      without any of the STM::WVar's that it reads being
      changed by another thread.  If any of the STM::WVar's
      that it has read did change then the operation will be
      started over.  Note that this means that the operation
      must only carry out idempotent operations as there is a
      chance that it will be run multiple times.  Note that
      you can nest calls to STM::Atomically all you want but if
      nested calls are retried or have conflicts then they
      will be abandoned and the operation for the first call
      to STM:WAtomic will be retried.  To abort the atomic
      operation just throw any exception and catch it outside
      of the call to STM::Atomically.  The return value of
      STM::Atomically will be the value returned by the operation
      being run.
			
      @param op The operation to carry out.  This function
      must have signature "result f(WAtomic&)" where result is
      either void a copy constructable type. It must also be
      compatible with boost::result_of. Note that this
      precludes wrapping the operation in boost::ref.

      @param args Arguments that set various limits, see
      WAtomicallyArgs for details.

		@return The result of op.
   */
   template <typename Op_t>
   void Atomically(const Op_t& op,
                   const WAtomicallyArgs& args = WAtomicallyArgs(),
                   typename
                   boost::enable_if<
                   boost::is_same<void,
                   typename Internal::WAtomicOpResultType<Op_t>::type> >::type* = 0)
   {
      WAtomic::AtomicallyImpl(boost::cref(op), args);
   }

   template <typename Op_t>
   typename Internal::WAtomicOpResultType<Op_t>::type
   Atomically(const Op_t& op,
              const WAtomicallyArgs& args = WAtomicallyArgs(),
              typename
              boost::disable_if<
              boost::is_same<void, typename Internal::WAtomicOpResultType<Op_t>::type> >::type* = 0)
   {
      typedef typename Internal::WAtomicOpResultType<Op_t>::type ResultType;
      Internal::WValOp<WAtomic, Op_t, ResultType, std::is_reference<ResultType>::value> val_op(op);
      WAtomic::AtomicallyImpl(boost::ref(val_op), args);
      return val_op.GetResult();
   }
   //!}

   //!{
   /**
      A function object that runs another function within
      STM::Atomically().

      @param Result_t The result type of the wrapped
      function.
   */
   template <typename Result_t>
   struct WRunAtomically
   {
      typedef Result_t result_type;

      /**
         Creates a RunAtomically object.

         @param Func_t The type of function to wrap.  The
         result type of this type must be Result_t and it
         must take STM::Atomic as its argument.

         @param f The function to wrap.
      */
      template <typename Func_t>
      WRunAtomically(Func_t f):
         m_func(f)
      {}

      /**
         Calls the atomically() passing it the wrapped
         function.
      */
      result_type operator()()
      {
         return Atomically(m_func);
      }

      boost::function<result_type (WAtomic&)> m_func;
   };

   template <>
   struct WRunAtomically<void>
   {
      typedef void result_type;

      /**
         Creates a RunAtomically object.

         @param Func_t The type of function to wrap.  The
         result type of this type must be void and it
         must take STM::Atomic as its argument.

         @param f The function to wrap.
      */
      template <typename Func_t>
      WRunAtomically(Func_t f):
         m_func(f)
      {}

      /**
         Calls the atomically() passing it the wrapped
         function.
      */
      void operator()()
      {
         Atomically(m_func);
      }

      boost::function<void (WAtomic&)> m_func;
   };
   //!}
			
   /**
      Convenience function for creating RunAtomically
      objects.
   */
   template <typename Func_t>
   WRunAtomically<typename Internal::WAtomicOpResultType<Func_t>::type>
   RunAtomically(const Func_t& f)
   {
      return WRunAtomically<typename Internal::WAtomicOpResultType<Func_t>::type>(f);
   }

   /**
      Returns true if the current thread is running under
      STM::atomically.
   */
   BSS_LIBAPI bool InAtomic();

   /**
      Exception thrown by WNoAtomic if it is constructed
      within a transaction.
   */
   struct BSS_CLASSAPI WInAtomicError : public WException
   {
      WInAtomicError();
   };

   /**
      Functions that cannot be called from within a
      transaction should take one of these as an argument,
      with a default constructed object as the default
      value. When this class is constructed it checks to see
      if a transaction exists and if it finds a transaction
      it throws WInAtomicError. Genrally this is used via the
      NO_ATOMIC macro.

      \sa NO_ATOMIC
   */
   class BSS_CLASSAPI WNoAtomic
   {
   public:
      WNoAtomic();
   };

   /**
      Use this as an argument in the declaration of functions
      that cannot be called from within a STM transaction. If
      the function is called an InAtomicError will be thrown.
   */
#define NO_ATOMIC const ::bss::thread::STM::WNoAtomic& = ::bss::thread::STM::WNoAtomic()
   /**
      If NO_ATOMIC is used in a function's declaration this
      macro shoudl be used for that argument in the
      function's definition.
   */
#define NO_ATOMIC_IMPL const ::bss::thread::STM::WNoAtomic&
            
   //!{
   /**
      Runs the given function in an "inconsistent"
      transaction.  This transaction is not committable and
      you cannot set WVar values when using this type of
      transaction.  Also the value read from a WVar is not
      saved in the transaction so multiple reads from the
      same WVar in one of these transactions can yield
      different values.  Note that reading the WVar values is
      still thread-safe, just not guaranteed consistent if
      the same WVar is read multiple times. This transaction
      also does not support Retry(), and since it does not
      commit it will not be re-run if read WVar's change.  It
      is also not allowed to call this function if you are
      already operating under an atomic transaction.  You can
      call Atomically from a function running under an
      inconsistent transaction, the atomic transaction will
      commit when the call to Atomically returns though.  This
      type of transaction should only be used when a bunch of
      variables need to be read and it doesn't matter that
      they can change while the reading is going on.
			   
      @param op The operation to run under an inconsistent
      transaction. Must have the signature type
      (WInconsistent&) where "type" can be void or any other
      type. It must also be compatible with
      boost::result_of. Note that this precludes wrapping the
      operation in boost::ref.

      @return The return value of op.
			   
      @throw WInAtomicError if this function is called from a
      function running under an atomic transaction.
   */
   template <typename Op_t>
   void Inconsistently(const Op_t& op,
                       NO_ATOMIC,
                       typename
                       boost::enable_if<
                       boost::is_same<void,
                       typename Internal::WInconsistentOpResultType<Op_t>::type> >::type* = 0)
   {
      WInconsistent::InconsistentlyImpl(boost::cref(op));
   }

   template <typename Op_t>
   typename Internal::WInconsistentOpResultType<Op_t>::type
   Inconsistently(const Op_t& op,
                  NO_ATOMIC,
                  typename
                  boost::disable_if<
                  boost::is_same<void,
                  typename Internal::WInconsistentOpResultType<Op_t>::type> >::type* = 0)
   {
      typedef typename Internal::WInconsistentOpResultType<Op_t>::type ResultType;
      Internal::WValOp<WInconsistent, Op_t, ResultType, std::is_reference<ResultType>::value> val_op(op);
      WInconsistent::InconsistentlyImpl(boost::ref(val_op));
      return val_op.GetResult();
   }
   //!}
			
   /**
      If a function passed to STM::Atomically determines that one
      of the STM::WVar's that it has read has a value that
      prevents the function from finishing then the function
      should call STM::Retry.  When STM::Retry is called the
      current transaction is abandoned and the thread is
      blocked until one of the STM::WVar's that was read is
      changed by another thread.  When a change is detected
      the blocked thread will be resumed and the atomic
      operation will be restarted.  Note that if there have
      been nested atomically calls then the outermost call will
      be the one restarted.
			
      @param wait An optional timeout value.  If the retry waits for
      this long then a WRetryTimeoutException will be thrown out of
      the outermost atomically call. By default this function waits
      forever.
   */
   BSS_LIBAPI void Retry(WAtomic& at, const WTimeArg& timeout = WTimeArg::UNLIMITED ());

   /**
      A transactional variable.  Access to the contents of
      the variable is restricted to functions passed to
      STM::Atomically, see the description of STM::Atomically for
      details on what "transactional" means.

      Note that WVar's are not copyable. This is intentional
      since the semantics of copying a WVar are not
      clear. Copying could mean copying the head value, but
      it is better for this to be done explicitly using
      Get/Set and the initializing constructor so that
      transaction use is explicit. Copying could also mean:
      making two WVar's share the same internals. But this
      seems dangerous and the same thing can be accomplished
      by using shared_ptr's to WVars. Using shared_ptr's makes
      the WVar sharing much clearer as well.
			
      @param Type_t The type stored. This type must be
      copyable and must not use STM::Atomically in its copy
      constructor or its destructor.
   */
   template <typename Type_t>
   class WVar
   {
   public:
      DEFINE_POINTERS (WVar);

      friend class WAtomic;
      
      typedef Type_t Type;
      typedef typename boost::call_traits<Type>::param_type param_type;
		
      /**
         Default Constructor.  This can only be used if
         Type_t has a default constructor.
      */
      WVar():
         m_core_p(stm_new Internal::WVarCore<Type_t>(
                     stm_new Internal::WValue<Type_t> (0, Type_t ())))
      {
         LOG_MEMORY_ALLOCATION (WVarCore, m_core_p.get ());
         LOG_MEMORY_ALLOCATION (WValue, m_core_p->m_value_p.get ());
      }

      /**
         Constructor.
					
         @param val The initial value for the variable.
      */
      explicit WVar(param_type val):
         m_core_p(stm_new Internal::WVarCore<Type_t>(stm_new Internal::WValue<Type_t> (0, val)))
      {
         LOG_MEMORY_ALLOCATION (WVarCore, m_core_p.get ());
         LOG_MEMORY_ALLOCATION (WValue, m_core_p->m_value_p.get ());
      }

      WVar (WVar&& var):
         m_core_p (std::move (var.m_core_p))
      {}

      WVar& operator=(WVar&& var)
      {
         m_core_p = std::move (var.m_core_p);
         return *this;
      }
      
      /**
         Gets the variable's current value.

         @param at The transaction to use.
      */
      param_type Get(WAtomic& at) const
      {
         const Internal::WValue<Type_t>* val_p =
            static_cast<const Internal::WValue<Type_t>*>(at.GetVarValue (m_core_p));
         if (!val_p)
         {
            at.ReadLock ();
            const Internal::WValue<Type_t>::Ptr value_p = m_core_p->m_value_p;
            at.ReadUnlock ();
            at.SetVarGetValue (m_core_p, value_p);
            val_p = value_p.get ();
         }
         return val_p->m_value;
      }

      /**
         Gets the variable's current value. Can only be used
         within a call to STM::Inconsistently.  Multiple
         calls to this for the same Var object can result in
         different values under the same transaction.

         @param ins The WInconsistent object currently in
         use.
      */
      Type GetInconsistent(WInconsistent& ins) const
      {
         ins.ReadLock();
         const Internal::WValue<Type_t>::ConstPtr val_p = m_core_p->m_value_p;
         ins.ReadUnlock();
         return val_p->m_value;
      }

      /**
         Gets the variable's current value.  This version
         can be used outside of a call to STM::Atomically in
         order to get the value in a read-only fashion.
         Note this function is slower than calling Get()
         from within a call to STM::Atomically.
      */
      Type GetReadOnly() const
      {
         return Atomically(WRead(*this));
      }

      /**
         Sets the value of the variable. Can only be used
         within a call to STM::Atomically.
				
         @param val The value to set, this value will be
         copied.
         @param at The transaction to use.
      */
      void Set(param_type val, WAtomic& at)
      {
         //DO NOT TRY TO USE R-VALUE REFS AND MOVES.
         //Moves are unusable in this case due to transaction
         //restarts. If a movable object is passed into a transaction,
         //moved into a WVar and then the transaction gets restarted
         //the object that we need to move from is now in its
         //post-move state and unusable.
         Internal::WValue<Type_t>* val_p =
            static_cast<Internal::WValue<Type_t>*>(at.GetVarSetValue (m_core_p));
         if (!val_p)
         {
            at.ReadLock ();
            val_p = stm_new Internal::WValue<Type_t>(m_core_p->m_value_p->m_version + 1, val);
            LOG_MEMORY_ALLOCATION (WValue, val_p);
            at.ReadUnlock ();
            at.SetVarValue (m_core_p, typename Internal::WValue<Type_t>::Ptr (val_p));
         }
         else
         {
            val_p->m_value = val;
         }
      }

      /**
         Sets the value of the variable. Creates a
         transaction to do this in so it will be slower than
         the other version of set if you are already in a
         transaction.
				   
         @param val The value to set, this value will be
         copied.
      */
      void Set(param_type val)
      {
         Atomically(WSet(val, *this));
      }

      /**
       * Validates just this variable. This can be useful if you need to watch just the validity of
       * a single variable during a long transaction and a full validation would be prohibitively
       * expensive. Note that a full validation will be done at the end of the transaction whether
       * this method is called or not.
       */
      void Validate (WAtomic& at) const
      {
         const auto val_p = at.GetVarGotValue (m_core_p);
         if (val_p)
         {
            at.ReadLock ();
            const auto valid = m_core_p->Validate (*val_p);
            at.ReadUnlock ();
            if (!valid)
            {
               throw Internal::WFailedValidationException();
            }
         }
      }
      
   private:
      //NOT COPYABLE
      WVar (const WVar&);
      WVar& operator= (const WVar&);
      
      struct WSet
      {
         typedef void result_type;
         param_type m_val;
         WVar& m_var;
         WSet(param_type val, WVar& var):
            m_val(val), m_var(var) 
         {}

         void operator()(WAtomic& at) const
         {
            m_var.Set(m_val, at);
         }

      private:
         WSet& operator= (const WSet&) { return *this; } // Silence warning C4512
      };

      struct WRead
      {
         typedef Type result_type;
         const WVar& m_var;
         WRead(const WVar& var): m_var(var) {}
						
         Type operator()(WAtomic& at) const
         {
            Type val = m_var.Get(at);
            return val;
         }

      private:
         WRead& operator= (const WRead&) { return *this; } // Silence warning C4512
      };

      typename Internal::WVarCore<Type_t>::Ptr m_core_p;
   };

   /**
    * A variable that has values "local" to a given transaction, sort of like
    * boost::thread_specific_ptr but for transactions instead of threads. The variable starts out
    * empty at the start of a transaction, in this state Get will return a null pointer, and stays
    * that way until Set is called, after which Get will return a pointer to the set value. Any
    * value that is set will only last until the transaction ends. Note that any value that is set
    * will be gone by time the "after" actions for the transaction run, but will still be available
    * during BeforeCommit actions. In child transactions the variable will contain the parent
    * transaction's value at the start and any value set will become the parent's value when the
    * child transaction commits. If the child transaction aborts then any values set in it will be
    * thrown away and the parent transaction will continue to see the same value as it saw before
    * the child transaction started.
    */
   template <typename Type_t>
   class WTransactionLocalValue
   {
      NO_COPYING (WTransactionLocalValue);
      
   public:
      WTransactionLocalValue ():
         m_key (Internal::GetTransactionLocalKey ())
      {}

      //!{
      /**
       * Gets the current value of variable.
       *
       * @param at The current STM transaction.
       *
       * @return A pointer to the current value, will be null if no value has been set for this
       * variable in the current transaction or one of it's parents.
       */
      Type_t* Get (WAtomic& at)
      {
         if (auto value_p = at.GetLocalValue (m_key))
         {
            return &static_cast<WValue*>(value_p)->m_value;
         }
         else
         {
            return nullptr;
         }
      }

      const Type_t* Get (WAtomic& at) const
      {
         if (auto value_p = at.GetLocalValue (m_key))
         {
            return &static_cast<WValue*>(value_p)->m_value;
         }
         else
         {
            return nullptr;
         }
      }
      //!}

      //!{
      /**
       * Sets the value for the current transaction.
       *
       * @param value The value to set. This value will either be copied or moved depending on which
       * variant of set was called.
       *
       * @return A reference to the new value in the variable.
       */
      Type_t& Set (const Type_t& value, WAtomic& at)
      {
         auto value_p = std::unique_ptr<WValue>(new WValue (value));
         auto& result = value_p->m_value;
         at.SetLocalValue (m_key, std::move (value_p));
         return result;
      }
      
      Type_t& Set (Type_t&& value, WAtomic& at)
      {
         auto value_p = std::unique_ptr<WValue>(new WValue (std::move (value)));
         auto& result = value_p->m_value;
         at.SetLocalValue (m_key, std::move (value_p));
         return result;
      }
      //!}
      
   private:
      //We use an integer key here instead of the this pointer to avoid "reused memory" issues. Say
      //we create a WTransactionLocalValue in a transaction, set its value, destroy it, and then
      //create another WTransactionLocalValue. There is a chance that the second
      //WTransactionLocalValue will have the same address as the first and will thus pick up the
      //value for the first that is still in the transaction object's local value hash map. This
      //would be problematic if Type_t is the same between the two locals, and disastrous if they
      //are different. So instead we use an integer key that will be different for every
      //WTransactionLocalValue object that is created. The key is 64 bit so we don't have to worry
      //about it rolling over in any reasonable amount of time.
      uint64_t m_key;
      
      struct WValue : public Internal::WLocalValueBase
      {
         Type_t m_value;

         explicit WValue (const Type_t& value):
            m_value (value)
         {}

         explicit WValue (Type_t&& value):
            m_value (std::move (value))
         {}
      };

   };

   /**
    * A transaction local flag. This flag is only visible within this transaction and its parents
    * and children. This method is here for those cases where an operation needs to be done at most
    * once per transaction.
    */
   class BSS_CLASSAPI WTransactionLocalFlag
   {
   public:
      /**
       * Sets the flag and returns the prior value of the flag.
       */
      bool TestAndSet (WAtomic& at);
      
   private:
      WTransactionLocalValue<bool> m_flag;
   };

}}}

#ifdef WIN32
#pragma warning (pop)
#endif
