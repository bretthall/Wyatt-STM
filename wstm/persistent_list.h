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

#include "exception.h"

#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/reverse_iterator.hpp>
#include <boost/call_traits.hpp>

#include <utility>
#include <iostream>
#include <stack>
#include <list>
#include <memory>
#include <algorithm>

/**
 * @file persistent_list.h
 * A persistent list implementation.
 */

namespace WSTM 
{
   /**
    * @defgroup PersistList Persistent List
    *
    * A peristent list implementation used internally by the STM system. 
    */
   ///@{

   /**
    * Exception thrown by WPersistentList methods when an invalid iterator is used.
    */
   struct WInvalidIteratorError : public WException
   {
      WInvalidIteratorError ();
   };

   /**
    * Exception thrown by WPersistentList methods when there is no element available for the given
    * operation.
    */
   struct WNoElementError : public WException
   {
      WNoElementError ();
   };

   /**
    * List that persists after modifications. What this means is that if you have a list and then
    * make a copy of it, modifying the first list will not modify the copy. This would not be so
    * exciting were it not for the fact that copying the list is O(1). And the lists share memory
    * for the nodes that come after any modified nodes. This makes PersistentList especially suited
    * to use in multiple threads, each thread gets its own cheap copy of the list that the other
    * thread connot modify. All of this comes with a couple of costs. The cost is that
    * modifications to the list are O(N), even when you have an iterator pointing to the
    * modification point.
    *
    * @param Value_t The type stored in the list. This should be immutable or a WVar in order to
    * avoid nasty surprises when using the list in multiple threads.
    */
   template <typename Value_t>
   class WPersistentList
   {
   public:
      using value_type = Value_t;
      using non_const_value_type = typename std::remove_const<Value_t>::type;
      using param_type = typename boost::call_traits<value_type>::param_type;
      
      /**
       * Creates an empty list.
       */
      WPersistentList():
         m_size(0)
      {}

      /**
         * Creates a list containing the elements in the given sequence. Note that this method will
         * use memory equivalent to the elements in the sequence while constructing the list.
         *
         * @param it The start of the sequence.
         *
         * @param end The end of the sequence.
         */
      template <typename Iter_t>
      WPersistentList(Iter_t it, Iter_t end_):
         m_size(0)
      {
         //this could be specialized for bidirectional iterators to be more efficient
         std::stack<value_type> vals;
         std::for_each(it, end_, [&](const value_type& val) {vals.push (val);});
         WNodePtr node_p;
         while(!vals.empty())
         {
            node_p = createWNode(vals.top(), node_p);
            vals.pop();
            ++m_size;
         }
         m_head_p = node_p;
      }

   private:
      // Forward declarations to satisfy the Intel compiler set.
      struct WZipper;

      struct WNode
      {
         using Ptr = std::shared_ptr<const WNode>;

         mutable non_const_value_type value;
         Ptr next_p;

         WNode(param_type value_, const Ptr& next_p_):
            value(value_), next_p(next_p_)
         {}
      };
      using WNodePtr = typename WNode::Ptr;

   public:
      /**
       * Checks if the list is empty.
       * 
       * @return true if the list is empty, false otherwise.
       */
      bool empty() const
      {
         return !m_head_p;
      }

      /**
       * Gets the size of the list. Note that this method is O(1).
       *
       * @return The size of the list.
       */
      size_t size() const
      {
         return m_size;
      }

      /**
       * Pushes the given element on the front of the list. This method is O(1).
       *
       * @param value The value to push.
       */
      void push_front(param_type value)
      {
         m_head_p = createWNode(value, m_head_p);
         ++m_size;
      }

      /**
       * Gets the element on the front of the list. This method is O(1).
       *
       * @return The first element in the list.
       *
       * @throw WNoElementError if the list is empty.
      */
      param_type front() const
      {
         if(m_head_p)
         {
            return m_head_p->value;
         }
         else
         {
            throw WNoElementError();
         }
      }

      /**
       * Pops the first element off the front of the list. This method is O(1).
       */
      void pop_front()
      {
         if(m_head_p)
         {
            m_head_p = m_head_p->next_p;
            --m_size;
         }
      }
         
      /**
       * Pushes the given element on the back of the list. This method is O(N).
       *
       * @param value The value to append.
       */
      void push_back(param_type value)
      {
         WZipper zip = zipToEnd();
         WNodePtr node_p = createWNode(value);
         while(zip.hasPrevious())
         {
            zip.previous();
            node_p = createWNode(zip.node_p->value, node_p);
         }
         m_head_p = node_p;
         ++m_size;
      }

      /**
       * Returns the element on the back of the list. This method is O(N).
       *
       * @param The last element in the list. 
       *
       * @throw WNoElementError if the list is empty.
       */
      param_type back() const
      {
         if(m_head_p)
         {
            return lastWNode(m_head_p)->value;
         }
         else
         {
            throw WNoElementError();
         }
      }

      /**
       * Pops the last element off the back of the list. This method is O(N).
       */
      void pop_back()
      {
         if(m_head_p && m_head_p->next_p)
         {
            WZipper zip = zipToEnd();
            zip.previous();
            WNodePtr node_p;
            while(zip.hasPrevious())
            {
               zip.previous();
               node_p = createWNode(zip.node_p->value, node_p);
            }
            m_head_p = node_p;
            --m_size;
         }
         else
         {
            m_head_p.reset();
            m_size = 0;
         }
      }
         
      /**
       * Concatenates the given list onto the back of this list. This method is O(N) where N is the
       * length of this list.
       *
       * @param list The list to concatentate.
       */
      void concat(const WPersistentList& list)
      {
         if(!list.empty())
         {
            WZipper zip = zipToEnd();
            WNodePtr node_p = list.m_head_p;
            while(zip.hasPrevious())
            {
               zip.previous();
               node_p = createWNode(zip.node_p->value, node_p);
            }
            m_head_p = node_p;
            m_size += list.m_size;
         }
      }

      /**
       * Compares to lists for equality. Two list are considered equal if they have the same number
       * of elements and corresponding elements in each list are equal.
       *
       * @param l The list to compare with.
       *
       * @return true if the list are equal, false otherwise.
      */
      bool operator==(const WPersistentList& l) const
      {
         if(m_head_p == l.m_head_p)
         {
            return true;
         }
         if(m_size == l.m_size)
         {
            WNodePtr n1_p = m_head_p;
            WNodePtr n2_p = l.m_head_p;
            while(n1_p)
            {
               if((n1_p != n2_p) && (n1_p->value != n2_p->value))
               {
                  return false;
               }
               n1_p = n1_p->next_p;
               n2_p = n2_p->next_p;
            }
            return true;
         }
         else
         {
            return false;
         }
      }

      /**
       * Compares to lists for inequality. Two list are considered equal if they have the same number
       * of elements and corresponding elements in each list are equal.
       *
       * @param l The list to compare with.
       *
       * @return true if the list are not equal, false otherwise.
       */
      bool operator!=(const WPersistentList& l) const
      {
         return !(*this == l);
      }
         
      template <typename> friend class iter;

      /**
       * Base for iterators, use iterator and const_iterator instead.
       */
      template <typename IterValue_t>
      class iter : public boost::iterator_facade<iter<IterValue_t>,
                                                 IterValue_t,
                                                 boost::bidirectional_traversal_tag,
                                                 IterValue_t&,
                                                 ptrdiff_t>
      {
      private:
         struct enabler
         {};
         
      public:
         using difference_type = ptrdiff_t;
         
         iter()
         {}
            
         template <typename OtherValue_t>
         iter(const iter<OtherValue_t>& other,
              typename std::enable_if<
              std::is_convertible<OtherValue_t*, IterValue_t*>::value,
              enabler>::type = enabler()):
            m_zip(other.m_zip)
         {}
            
      private:            
         template <typename> friend class WPersistentList;
            
         iter(const typename WPersistentList::WZipper& zip):
            m_zip(zip)
         {}

         friend class boost::iterator_core_access;

         void increment()
         {
            if(!m_zip.atEnd())
            {
               m_zip.next();
            }
            else
            {
               throw WInvalidIteratorError();
            }
         }
            
         void decrement()
         {
            if(m_zip.hasPrevious())
            {
               m_zip.previous();
            }
            else
            {
               throw WInvalidIteratorError();
            }               
         }

         void advance(difference_type dist)
         {
            if(dist > 0)
            {
               for(difference_type i = 0 ; i < dist; ++i)
               {
                  increment();
               }
            }
            else if(dist < 0)
            {
               for(difference_type i = 0 ; i < -dist; ++i)
               {
                  decrement();
               }
            }
         }
            
         IterValue_t& dereference() const
         {
            if(!m_zip.atEnd())
            {
               return m_zip.node_p->value;
            }
            else
            {
               throw WInvalidIteratorError();
            }               
         }
            
         template <typename OtherValue_t>
         bool equal(const iter<OtherValue_t>& it) const
         {
            return m_zip == it.m_zip;
         }
            
         typename WPersistentList::WZipper m_zip;
      };

   //@{
   /**
    * Iterator over a WPersistentList. This iterator is bidirectional. Note that this iterator uses
    * O(N) memory where N is the iterator's distance from the start of the list. If you are just
    * iterating the list and not using the iterator to modify the list then take a look at
    * forward_iterator instead that is O(1) in memory usage. Iterator invalidation is a bit
    * different in WPersistentList. If a list is modified an iterator that was obtained from the
    * list can no longer be passed to methods that modify the list. The iterator can still be used
    * to iterate list elements, but the iterator will iterate the elements as they were before the
    * list was modified. Note that the list elements can be modified using the iterator but this is
    * a bad idea unless the elements are WVar object's since this nullifies the thread safety of the
    * list.
    */
   using iterator = iter<Value_t>;
   using const_iterator = iter<const Value_t>;
   //@}
         
   //@{
   /**
    * Creates an iterator pointing at the first element in the list.
    *
    * @return An iterator pointing at the front of the list.
    */
   iterator begin()
   {
      return iterator(WZipper(m_head_p));
   }
   const_iterator begin() const
   {
      return const_iterator(WZipper(m_head_p));
   }
   //@}
         
   //@{
   /**
    * Creates an iterator pointing at the end of the list.
    *
    * @return An iterator pointing at the end of the list.
    */
   iterator end()
   {
      WZipper zip = zipToEnd();
      zip.next();
      return iterator(zip);
   }
   const_iterator end() const
   {
      WZipper zip = zipToEnd();
      zip.next();
      return const_iterator(zip);
   }
   //@}
         
   template <typename> friend class forward_iter;

   /**
    * Base for forward iterators, use forward_iterator and const_forward_iterator instead.
    */
   template <typename IterValue_t>
   class forward_iter :
      public boost::iterator_facade<forward_iter<IterValue_t>,
                                    IterValue_t,
                                    boost::forward_traversal_tag>
   {
   private:
      struct enabler
      {};

   public:
      using difference_type = ptrdiff_t;

      forward_iter()
      {}

      template <typename OtherValue_t>
      forward_iter(const forward_iter<OtherValue_t>& other,
                   typename std::enable_if<
                   std::is_convertible<OtherValue_t*,IterValue_t*>::value
                   , enabler>::type = enabler()):
         m_node_p(other.m_node_p)
      {}
      
   private:
      template <typename> friend class WPersistentList;
            
      forward_iter(const typename WPersistentList::WNodePtr& node_p):
         m_node_p(node_p)
      {}

      friend class boost::iterator_core_access;

      void increment()
      {
         if(m_node_p)
         {
            m_node_p = m_node_p->next_p;
         }
         else
         {
            throw WInvalidIteratorError();
         }
      }
      
      void advance(difference_type dist)
      {
         if(dist > 0)
         {
            for(difference_type i = 0 ; i < dist; ++i)
            {
               increment();
            }
         }
         else if(dist < 0)
         {
            throw WInvalidIteratorError();
         }
      }
            
      IterValue_t& dereference() const
      {
         if(m_node_p)
         {
            return m_node_p->value;
         }
         else
         {
            throw WInvalidIteratorError();
         }               
      }
            
      template <typename OtherValue_t>
      bool equal(const forward_iter<OtherValue_t>& it) const
      {
         return m_node_p == it.m_node_p;
      }
            
      typename WPersistentList::WNodePtr m_node_p;
   };

   //@{
   /**
    * Iterator over a WPersistentList. This iterator moves forward only and cannot be used to modify
    * the list. On the other hand it doesn't have the O(N) memory usage that iterator does, instead
    * it has O(1) memory usage. Iterator invalidation is a bit different in WPersistentList. If a
    * list is modified an iterator that was obtained from the list can no longer be passed to
    * methods that modify the list. The iterator can still be used to iterate list elements, but the
    * iterator will iterate the elements as they were before the list was modified.
    */
   using forward_iterator = forward_iter<Value_t>;
   using const_forward_iterator = forward_iter<const Value_t>;
   //@}

   //@{
   /**
    * Creates an iterator pointing at the first element in the list.
    *
    * @return An iterator pointing at the front of the list.
    */
   forward_iterator fbegin()
   {
      return forward_iterator(m_head_p);
   }
   const_forward_iterator fbegin() const
   {
      return const_forward_iterator(m_head_p);
   }
   //@}
         
   //@{
   /**
    * Creates an iterator pointing at the end of the list.
    *
    * @return An iterator pointing at the end of the list.
    */
   forward_iterator fend()
   {
      return forward_iterator(WNodePtr());
   }
   const_forward_iterator fend() const
   {
      return const_forward_iterator(WNodePtr());
   }
   //@}
         
   //@{
   /**
    * Reverse iterator over a WPersistentList. This iterator is bidirectional. Note that this
    * iterator uses O(N) memory where N is the iterator's distance from the start of the list. If
    * you are just iterating the list and not using the iterator to modify the list then take a look
    * at forward_iterator instead that is O(1) in memory usage, there is no O(1)
    * reverse_iterator. Iterator invalidation is a bit different in PersistentList. If a list is
    * modified an iterator that was obtained from the list can no longer be passed to methods that
    * modify the list. The iterator can still be used to iterate list elements, but the iterator
    * will iterate the elements as they were before the list was modified.
   */
   using reverse_iterator = boost::reverse_iterator<iterator>;
   using const_reverse_iterator = boost::reverse_iterator<const_iterator>;
   //@}
         
   //@{
   /**
    * Creates a reverse iterator pointing at the last element in the list.
    *
    * @return A reverse iterator pointing at the last element of the list.
    */
   reverse_iterator rbegin()
   {
      return reverse_iterator(end());
   }
   const_reverse_iterator rbegin() const
   {
      return const_reverse_iterator(end());
   }
   //@}
         
   //@{
   /**
    * Creates a reverse iterator pointing at the beginning of the list.
    *
    * @return A reverse iterator pointing at the beginning of the list.
    */
   reverse_iterator rend()
   {
      return reverse_iterator(begin());
   }
   const_reverse_iterator rend() const
   {
      return const_reverse_iterator(begin());
   }
   //@}
         
   /**
    * Inserts the given value in the list at the given position. The method is O(N) where N is the
    * distance from the start of the list to pos.
    *
    * @param pos Iterator pointing at the position to insert the element before. This iterator will
    * no longer be valid with this list after this method returns.
    *
    * @param value The value to insert.
    *
    * @return An iterator pointing at the new element.
    *
    * @throw WInvalidIteratorError if pos is an invalid iterator.
    */
   iterator insert(const iterator& pos, param_type value)
   {
      WZipper zip = pos.m_zip;
      WNodePtr new_p = createWNode(value, zip.node_p);
      WNodePtr node_p = new_p;
      WZipper outZip(new_p);
      while(zip.hasPrevious())
      {
         zip.previous();
         node_p = createWNode(zip.node_p->value, node_p);
         outZip.before.push_back(node_p);
      }
      if(zip.node_p != m_head_p)
      {
         throw WInvalidIteratorError();
      }
      m_head_p = node_p;
      ++m_size;
      return outZip;
   }

   /**
    * Inserts the given sequence of values in the list at the given position. The method is O(N + M)
    * where N is the distance from the start of the list to pos and M is the length of the input
    * sequence.
    *
    * @param pos Iterator pointing at the position to insert the elements before. This iterator will
    * no longer be valid with this list after this method returns.
    *
    * @param it The start of the sequence to insert.
    *
    * @param end The end of the sequence to insert.
    *
    * @throw WInvalidIteratorError if pos is an invalid iterator.
   */
   template <typename Iter_t>
   void insert(const iterator& pos, Iter_t it, Iter_t end_)
   {
      WZipper zip = pos.m_zip;
      std::stack<value_type> vals;
      std::for_each(it, end_, [&](const value_type& val) {vals.push (val);});
      const auto count = vals.size();
      WNodePtr node_p = zip.node_p;
      while(!vals.empty())
      {
         node_p = createWNode(vals.top(), node_p);
         vals.pop();
      }
      while(zip.hasPrevious())
      {
         zip.previous();
         node_p = createWNode(zip.node_p->value, node_p);
      }
      if(zip.node_p != m_head_p)
      {
         throw WInvalidIteratorError();
      }
      m_head_p = node_p;
      m_size += count;
   }
         
   /**
    * Replaces the given element with the given value. This method is O(N) where N is the distance
    * from the start of the list to pos. This method should be used when the list elements are
    * immutable, if the elements are WVar object's then this method is not nescessary and the
    * WVar object's should be directly modified.
    *
    * @param pos Iterator pointing at the element to replace. This iterator will no longer be valid
    * with this list after this method returns.
    *
    * @param value The value to replace the old value with.
    *
    * @return An iterator pointing at the updated element.
    *
    * @throw WInvalidIteratorError if pos is an invalid iterator.
   */
   iterator replace(const iterator& pos, param_type value)
   {
      WZipper zip = pos.m_zip;
      if(zip.node_p)
      {
         WNodePtr new_p = createWNode(value, zip.node_p->next_p);
         WNodePtr node_p = new_p;
         WZipper outZip(new_p);
         while(zip.hasPrevious())
         {
            zip.previous();
            node_p = createWNode(zip.node_p->value, node_p);
            outZip.before.push_back(node_p);
         }
         if(zip.node_p != m_head_p)
         {
            throw WInvalidIteratorError();
         }
         m_head_p = node_p;
         return outZip;
      }
      else
      {
         push_back(value);
         return begin();
      }
   }
                  
   /**
    * Erases all elements in the list.
    */
   void clear()
   {
      m_head_p.reset();
      m_size = 0;
   }

   /**
    * Erase the element at the given position. This method is O(N) where N is the distance from the
    * start of the list to pos.
    *
    * @param pos An iterator pointing at the element to erase. This iterator will no longer be valid
    * with this list after this method returns.
    *
    * @return An iterator pointing at the element after the erased element.
    *
    * @throw WInvalidIteratorError if pos is an invalid iterator.
   */
   iterator erase(const iterator& pos)
   {
      WZipper zip = pos.m_zip;
      if(zip.node_p)
      {
         WNodePtr next_p = zip.node_p->next_p;
         WNodePtr node_p = next_p;
         WZipper outZip(next_p);
         while(zip.hasPrevious())
         {
            zip.previous();
            node_p = createWNode(zip.node_p->value, node_p);
            outZip.before.push_back(node_p);
         }
         if(zip.node_p != m_head_p)
         {
            throw WInvalidIteratorError();
         }
         m_head_p = node_p;
         --m_size;
         return outZip;
      }
      else
      {
         throw WInvalidIteratorError();
      }            
   }

   /**
    * Erase the elements in the given sequence. This method is O(N) where N is the distance from the
    * start of the list to it.
    *
    * @param it An iterator pointing at the first element to erase. This iterator will no longer be
    * valid with this list after this method returns.
    *
    * @param end_ An iterator pointing at one past the last element to erase. This iterator will no
    * longer be valid with this list after this method returns.
    *
    * @return An iterator pointing at the element after the last erased element.
    *
    * @throw WInvalidIteratorError if either of it or end_ is an invalid iterator.
   */
   iterator erase(const iterator& it, const iterator& end_)
   {
      WZipper zip = it.m_zip;
      WZipper zipEnd = end_.m_zip;
      if(zip.node_p)
      {
         WNodePtr next_p = zipEnd.node_p;
         WNodePtr node_p = next_p;
         WZipper outZip(next_p);
         while(zip.hasPrevious())
         {
            zip.previous();
            node_p = createWNode(zip.node_p->value, node_p);
            outZip.before.push_back(node_p);
         }
         if(zip.node_p != m_head_p)
         {
            throw WInvalidIteratorError();
         }
         m_head_p = node_p;
         m_size -= std::distance(it, end_);
         return outZip;
      }
      else
      {
         throw WInvalidIteratorError();
      }            
   }
         
#ifdef TEST_MODE
   bool checkIter(const iterator& it)
   {
      WZipper zip = it.m_zip;
      while(zip.hasPrevious())
      {
         zip.previous();
      }
      return zip.node_p == m_head_p;
   }
#endif //TEST_MODE
         
private:
   WNodePtr createWNode(param_type value, const WNodePtr& next_p = WNodePtr()) const
   {
      return WNodePtr(new WNode(value, next_p));
   }
         
   WNodePtr m_head_p;
   size_t m_size;
         
   friend struct WZipper;

   struct WZipper
   {
      std::list<WNodePtr> before;
      WNodePtr node_p;

      WZipper(const WNodePtr& n_p,
              const std::list<WNodePtr>& before_ = std::list<WNodePtr>()):
         before(before_), node_p(n_p)
      {}

      void next()
      {
         if(node_p)
         {
            before.push_front(node_p);
            node_p = node_p->next_p;
         }
      }

      void previous()
      {
         if(!before.empty())
         {
            node_p = before.front();
            before.pop_front();
         }
      }

      bool hasNext() const
      {
         return node_p->next_p;
      }

      bool hasPrevious() const
      {
         return !before.empty();
      }

      bool atEnd() const
      {
         return !node_p;
      }

      bool operator==(const WZipper& z) const
      {
         return (node_p == z.node_p) && (before == z.before);
      }
   };

   WZipper zipToEnd() const
   {
      WZipper zip(m_head_p);
      while(!zip.atEnd())
      {
         zip.next();
      }
      return zip;
   }

   WNodePtr lastWNode(WNodePtr node_p) const
   {
      if(node_p)
      {
         while(node_p->next_p)
         {
            node_p = node_p->next_p;
         }
      }
      return node_p;
   }
         
};
      
/**
 * Creates a new PersistentList with the given value pushed on the front. This function is O(1).
 *
 * @param value The value to put on the front of the list.
 *
 * @param list The list to push value on the front of.
 *
 * @return A copy of the given list with the given value on the front.
*/
template <typename Value_t>
WPersistentList<Value_t> push_front(typename WPersistentList<Value_t>::param_type value, const WPersistentList<Value_t>& list)
{
   WPersistentList<Value_t> result = list;
   result.push_front(value);
   return result;
}
      
/**
 * Creates a new PersistentList equal to the given list without the first element. This function is
 * O(1).
 *
 * @param list The list to pop the value off the front of.
 *
 * @return A copy of the given list without the first element.
 */
template <typename Value_t>
WPersistentList<Value_t> pop_front(const WPersistentList<Value_t>& list)
{
   WPersistentList<Value_t> result = list;
   result.pop_front();
   return result;
}

/**
 * Creates a new PersistentList with the given value pushed on the back. This function is O(N).
 *
 * @param value The value to put on the back of the list.
 *
 * @param list The list to push value on the back of.
 *
 * @return A copy of the given list with the given value appended
*/
template <typename Value_t>
WPersistentList<Value_t> push_back(typename WPersistentList<Value_t>::param_type value,
                                   const WPersistentList<Value_t>& list)
{
   WPersistentList<Value_t> result = list;
   result.push_back(value);
   return result;
}

/**
 * Creates a new PersistentList equal to the given list without the last element. This function is
 * O(N).
 *
 * @param list The list to pop the value off the back of.
 *
 * @return A copy of the given list with the last value removed.
*/
template <typename Value_t>
WPersistentList<Value_t> pop_back(const WPersistentList<Value_t>& list)
{
   WPersistentList<Value_t> result = list;
   result.pop_back();
   return result;
}

/**
 * Splits the first element off of a list.
 *
 * @param list The list to split the head off of.
 *
 * @return A pair consisting of the first element in the list and a list containing the rest of the
 * elements.
 *
 * @throw WNoElementError if list is empty.
*/
template <typename Value_t>
std::pair<typename WPersistentList<Value_t>::param_type,  WPersistentList<Value_t> >
splitHead(const WPersistentList<Value_t>& list)
{
   return std::make_pair(list.front(), pop_front(list));
}
      
/**
 * Concatenates the two lists.
 *
 * @param l1 The first list to concatenate.
 *
 * @param l2 The second list to concatenate.
 * 
 * @return The concatenation of l1 and l2.
*/
template <typename Value_t>
WPersistentList<Value_t> operator+(const WPersistentList<Value_t>& l1,
                                   const WPersistentList<Value_t>& l2)
{
   WPersistentList<Value_t> result = l1;
   result.concat(l2);
   return result;
}

#ifdef TEST_MODE
template <typename Value_t>
std::ostream& operator<<(std::ostream& out, const WPersistentList<Value_t>& l)
{
   out << "PersistentList(";
   if(!l.empty())
   {
      out << l.front();
      for(auto it = ++l.fbegin(); it != l.fend(); ++it)
      {
         out << "," << *it;
      }
   }
   out << ")";
   return out;
}
#endif //TEST_MODE

   ///@}
}


