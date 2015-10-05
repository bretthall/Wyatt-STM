/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "BSS/Thread/STM/stm.h"

#include "BSS/Common/STLUtils.h"
#include "BSS/Thread/Thread.h"
#include "BSS/wtcbss.h"

#include <boost/call_traits.hpp>
#include <boost/type_traits/is_convertible.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/iterator/reverse_iterator.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

namespace bss
{
	namespace thread
	{
      namespace STM
      {
         /**
            Exception thrown if a STMList iterator goes off the end of
            the list.
         */
         class BSS_CLASSAPI WSTMListOutOfBoundsError : public std::exception
         {
         public:
            WSTMListOutOfBoundsError ();
         };

         /**
            A linked list where the list structure is transacted.  The
            contents of the list are not transacted unless Var's are
            stored in the list.  The interface matches std::list for
            the most part but most methods have a version that uses
            the current transaction instead of creating a new
            one. Iterators also are restricted to only being used
            within one transaction. Note that the method naming in
            this class follows the STL style for methods that are in
            std::list and then other methods use the wyatt style.

            @param Value_t The type stored in the list.
         */
         template <typename Value_t>
         class WSTMList
         {
         public:
            //!The type stored in the list.
            typedef Value_t value_type;
            //!Pointer to the type stored in the list.
            typedef Value_t* pointer;
            //!Reference to the type stored in the list.
            typedef Value_t& reference;
            //!Const reference to the type stored in the list.
            typedef const Value_t& const_reference;
            //!Type returned by size ().
            typedef size_t size_type;
            /**
               Type used when reporting the difference in position of
               two iterators.
            */
            typedef int difference_type;

            typedef typename boost::call_traits<value_type>::param_type param_type;

            // Forward declaration to satisfy Intel compiler.
            struct WNode;

            /**
               Creates an empty list.
            */
            WSTMList ():
               m_size_v (0)
            {}

            //!{
            /**
               Creates a list that is a copy of the given list.

               @param list The list to be copied.

               @param at The transaction to make the copy in, if not
               provided a new transaction will be used.
            */
            WSTMList (const WSTMList& list):
               m_size_v (0)
            {
               Atomically (boost::bind (&WSTMList::copy, this, boost::cref (list), _1));
            }

            WSTMList (const WSTMList& list, WAtomic& at):
               m_size_v (0)
            {
               copy (list, at);
            }
            //!}
			
            /**
               Creates a new list iwth the given number of default
               constructed elements.  This constructor can only be
               used when Value_t is default constructable.

               @param initialSize The number of default constructed
               elements to put in the list.
            */
            explicit WSTMList (size_type initialSize):
               m_size_v (0)
            {
               SingleValue val (Value_t (), initialSize);
               Atomically (boost::bind (&WSTMList::InsertNodes<SingleValue>, this,
                                        boost::ref (val), m_empty_p, _1));
            }

            /**
               Creates a list with the given number of copies of the
               given element.

               @param num The number of element to put in the list.

               @param val The value to copy into all the list elements.
            */
            WSTMList (size_type num, param_type val):
               m_size_v (0)
            {
               SingleValue sval (val, num);
               Atomically (boost::bind (&WSTMList::InsertNodes<SingleValue>, this,
                                        boost::ref (sval), m_empty_p, _1));
            }

            //!{
            /**
               Creates a list filled with the values in the given sequence.

               @param it The start of the sequence of values to copy.

               @param end The end of the sequence of values to copy.

               @param at The transaction to copy the elements in, if
               not given a new transaction will be used.
            */
            template <typename Iter_t>
            WSTMList (Iter_t it, Iter_t end):
               m_size_v (0)
            {
               if (it != end)
               {
                  IterValues<Iter_t> vals (it, end);
                  Atomically (boost::bind (&WSTMList::InsertNodes<IterValues<Iter_t> >, this,
                                           boost::ref (vals), m_empty_p, _1));
               }
            }
			
            template <typename Iter_t>
            WSTMList (Iter_t it, Iter_t end, WAtomic& at):
               m_size_v (0)
            {
               if (it != end)
               {
                  auto vals = IterValues<Iter_t> (it, end);
                  InsertNodes (vals, m_empty_p, at);
               }
            }
            //!}
			
            //{!
            /**
               Tests whether the list is empty.

               @param at The transaction to use, if not provided a new
               transaction will be used.

               @return true If the list has no elements, false otherwise.
            */
            bool empty () const
            {
               return Atomically (boost::bind (&WSTMList::EmptyAt, this, _1));
            }
			
            bool empty (WAtomic& at) const
            {
               return EmptyAt (at);
            }			
            //!}
			
            //!{
            /**
               Gets the number of elements in the list.
			   
               @param at The transaction to use, if not provided a new
               transaction will be used.
			   
               return The number of elements in the list.
            */
            size_type size () const
            {
               return Atomically (boost::bind (&WSTMList::SizeAt, this, _1));
            }
			
            size_type size (WAtomic& at) const
            {
               return SizeAt (at);
            }

            template <typename IterVal_t>
            class IterBase :
               public boost::iterator_facade<IterBase<IterVal_t>,
                                             IterVal_t,
                                             boost::bidirectional_traversal_tag>
            {
               friend class boost::iterator_core_access;
               template <typename> friend class IterBase;
               template <typename> friend class RevIterBase;
               template <typename> friend class WSTMList;

               typedef typename WSTMList<Value_t>::WNode Node;
               typedef typename Node::Ptr NodePtr;
            private:
               struct enabler {};
				
            public:
               IterBase ():
                  m_at_p (0)
               {}

               template <typename OtherVal_t>
               IterBase (const IterBase<OtherVal_t>& it,
                         typename boost::enable_if<
                            boost::is_convertible<OtherVal_t*, IterVal_t*>, enabler>::type =
                         enabler ()):
                  m_node_p (it.m_node_p), m_at_p (it.m_at_p)
               {}
				
            protected:

               IterBase (const NodePtr& node_p, WAtomic& at):
                  m_node_p (node_p), m_at_p (&at)
               {}

               void increment ()
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }
                  m_node_p = (m_node_p ? m_node_p->m_next_v.Get (*m_at_p) : m_node_p);
               }

               void decrement ()
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }
                  m_node_p = (m_node_p ? m_node_p->m_prev_v.Get (*m_at_p).lock () : m_node_p);
               }

               void advance (difference_type n)
               {
                  if (n > 0)
                  {
                     for (difference_type i = 0; i != n; ++i)
                     {
                        increment ();
                     }
                  }
                  else
                  {
                     for (difference_type i = 0; i != n; --i)
                     {
                        decrement ();
                     }
                  }
               }

               template <typename OtherVal_t>
               bool equal (const IterBase<OtherVal_t>& it) const
               {
                  return it.m_node_p == m_node_p;
               }

               IterVal_t& dereference () const
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }					
                  return m_node_p->m_val;
               }

               NodePtr m_node_p;
               WAtomic* m_at_p;
            };

            //!{
            /**
               Forward iterators for the list.  Note that WSTMList
               iterators are only valid within the transaction that
               they are created in.
            */
            typedef IterBase<value_type> iterator;
            typedef IterBase<const value_type> const_iterator;
            //!}
			
            template <typename IterVal_t>
            class RevIterBase :
               public boost::iterator_facade<RevIterBase<IterVal_t>,
                                             IterVal_t,
                                             boost::bidirectional_traversal_tag>
            {
               friend class boost::iterator_core_access;
               template <typename> friend class IterBase;
               template <typename> friend class RevIterBase;
               template <typename> friend class WSTMList;

               typedef typename WSTMList<Value_t>::WNode Node;
               typedef typename Node::Ptr NodePtr;
            private:
               struct enabler {};
				
            public:
               RevIterBase ():
                  m_at_p (0)
               {}

               template <typename OtherVal_t>
               RevIterBase (const RevIterBase<OtherVal_t>& it,
                            typename boost::enable_if<
                               boost::is_convertible<OtherVal_t*, IterVal_t*>, enabler>::type =
                            enabler ()):
                  m_node_p (it.m_node_p), m_at_p (it.m_at_p)
               {}

               template <typename OtherVal_t>
               RevIterBase (const IterBase<OtherVal_t>& it,
                            typename boost::enable_if<
                               boost::is_convertible<OtherVal_t*, IterVal_t*>, enabler>::type =
                            enabler ()):
                  m_node_p (it.m_node_p), m_at_p (it.m_at_p)
               {}

               IterBase<IterVal_t> base () const
               {
                  if (!m_at_p || !m_node_p)
                  {
                     return IterBase<IterVal_t> ();
                  }
                  else
                  {
                     return IterBase<IterVal_t> (m_node_p->m_next_v.Get (*m_at_p), *m_at_p);
                  }
               }

            protected:

               RevIterBase (const NodePtr& node_p, WAtomic& at):
                  m_node_p (node_p), m_at_p (&at)
               {}

               void increment ()
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }
                  m_node_p = (m_node_p ? m_node_p->m_prev_v.Get (*m_at_p).lock () : m_node_p);
               }

               void decrement ()
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }
                  m_node_p = (m_node_p ? m_node_p->m_next_v.Get (*m_at_p) : m_node_p);
               }

               void advance (difference_type n)
               {
                  if (n > 0)
                  {
                     for (difference_type i = 0; i != n; ++i)
                     {
                        increment ();
                     }
                  }
                  else
                  {
                     for (difference_type i = 0; i != n; --i)
                     {
                        decrement ();
                     }
                  }
               }

               template <typename OtherVal_t>
               bool equal (const RevIterBase<OtherVal_t>& it) const
               {
                  return it.m_node_p == m_node_p;
               }

               IterVal_t& dereference () const
               {
                  if (!m_at_p || !m_node_p)
                  {
                     throw WSTMListOutOfBoundsError ();
                  }					
                  return m_node_p->m_val;
               }

               NodePtr m_node_p;
               WAtomic* m_at_p;
            };
			
            //!{
            /**
               Reverse iterators for the list.  Note that WSTMList
               iterators are only valid within the transaction that
               they are created in.
            */
            typedef RevIterBase<value_type> reverse_iterator;
            typedef RevIterBase<const value_type> const_reverse_iterator;
            //!}
			
            //!{
            /**
               Gets an iterator pointing at the first element in the
               list.

               @param at The transaction to use.
            */
            iterator begin (WAtomic& at)
            {
               return iterator (m_head_v.Get (at), at);
            }
			
            const_iterator begin (WAtomic& at) const
            {
               return const_iterator (m_head_v.Get (at), at);
            }
            //!}
			
            //!{
            /**
               Gets an iterator pointing at one past the last element
               in the list.

               @param at The transaction to use.
            */
            iterator end (WAtomic&)
            {
               return iterator ();
            }
			
            const_iterator end (WAtomic&) const
            {
               return const_iterator ();
            }
            //!}
			
            //!{
            /**
               Gets a reverse iterator pointing at the last element in
               the list.

               @param at The transaction to use.
            */
            reverse_iterator rbegin (WAtomic& at)
            {
               return reverse_iterator (m_tail_v.Get (at), at);
            }
			
            const_reverse_iterator rbegin (WAtomic& at) const
            {
               return const_reverse_iterator (m_tail_v.Get (at), at);
            }
            //!}
			
            //!{
            /**
               Gets a reverse iterator pointing at one past the first
               element in the list.

               @param at The transaction to use.
            */
            reverse_iterator rend (WAtomic& /*at*/)
            {
               return reverse_iterator ();
            }
			
            const_reverse_iterator rend (WAtomic& /*at*/) const
            {
               return const_reverse_iterator ();
            }
            //!}
			
            //!{
            /**
               Pushes the given value on the front of the list.

               @param val The value to put in the list.

               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void push_front (param_type val)
            {
               Atomically (boost::bind (&WSTMList::PushFrontAt, this,
                                        bss::STLUtils::WrapIfRef<param_type>::wrap (val), _1));	
            }
			
            void push_front (param_type val, WAtomic& at)
            {
               PushFrontAt (val, at);
            }
            //!}
			
            //!{
            /**
               Pushes the given value on the back of the list.

               @param val The value to put in the list.

               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void push_back (param_type val)
            {
               Atomically (boost::bind (&WSTMList::PushBackAt, this,
                                        bss::STLUtils::WrapIfRef<param_type>::wrap (val), _1));
            }
			
            void push_back (param_type val, WAtomic& at)
            {
               PushBackAt (val, at);
            }
            //!}

            //!{
            /**
               Removes the first element from the list.
			   
               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void pop_front ()
            {
               Atomically (boost::bind (&WSTMList::PopFrontAt, this, _1));
            }
			
            void pop_front (WAtomic& at)
            {
               PopFrontAt (at);
            }
            //!}
			
            //!{
            /**
               Removes the last element from the list.
			   
               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void pop_back ()
            {
               Atomically (boost::bind (&WSTMList::PopBackAt, this, _1));
            }
			
            void pop_back (WAtomic& at)
            {
               PopBackAt (at);
            }
            //!}
			
            //!{
            /**
               Swaps the contents of this list with the given list.

               @param list The list to swap contents with.

               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void swap (WSTMList& list)
            {
               Atomically (boost::bind (&WSTMList::SwapAt, this, boost::ref (list), _1));
            }

            void swap (WSTMList& list, WAtomic& at)
            {
               SwapAt (list, at);
            }
            //!}
			
            //!{
            /**
               Gets the element on the front of the list.
			   
               @param at The transaction to use, if not provided a new
               transaction will be used.

               @return The value at the front of the list.

               @throws WSTMListOutOfBoundsError If the list is empty.
            */
            reference front ()
            {
               return Atomically (boost::bind (&WSTMList::FrontAt, this, _1));
            }
			
            reference front (WAtomic& at)
            {
               return FrontAt (at);
            }
			
            param_type front () const
            {
               return Atomically (boost::bind (&WSTMList::ConstFrontAt, this, _1));
            }
			
            param_type front (WAtomic& at) const
            {
               return ConstFrontAt (at);
            }
            //!}
			
            //!{
            /**
               Gets the element on the back of the list.
			   
               @param at The transaction to use, if not provided a new
               transaction will be used.

               @return The value at the back of the list.

               @throws WSTMListOutOfBoundsError If the list is empty.
            */
            reference back ()
            {
               return Atomically (boost::bind (&WSTMList::BackAt, this, _1));
            }
			
            reference back (WAtomic& at)
            {
               return BackAt (at);
            }
			
            param_type back () const
            {
               return Atomically (boost::bind (&WSTMList::ConstBackAt, this, _1));
            }
			
            param_type back (WAtomic& at) const
            {
               return ConstBackAt (at);
            }
            //!}
			
            /**
               Inserts a value into the list.

               @param pos The value will be inserted into the list in
               front of this element.

               @param val The value to insert.

               @param at The transaction to use.

               @return An iterator pointing at the new element.
            */
            iterator insert (const iterator& pos, param_type val, WAtomic& at)
            {
               auto vals = SingleValue (val, 1);
               return iterator (InsertNodes (vals, pos.m_node_p, at), at);
            }

            /**
               Inserts values into the list.

               @param pos The values will be inserted into the list in
               front of this element.

               @param it The start of the sequence of values to insert.

               @param end The end of the sequence of values to insert.

               @param at The transaction to use.
            */
            template <typename Iter_t>
            void insert (const iterator& pos, Iter_t it, Iter_t end, WAtomic& at,
                        typename boost::disable_if<
                        boost::is_convertible<Iter_t, Value_t>, int>::type = 0)
            {
               if (it == end)
               {
                  return;
               }

               auto vals = IterValues<Iter_t> (it, end);
               InsertNodes (vals, pos.m_node_p, at);
            }

            /**
               Inserts the given number of copies of the given value
               into the list.

               @param pos The values will be inserted into the list in
               front of this element.

               @param num The number of elements to insert.

               @param val The value to copy into the new elements.
			   
               @param at The transaction to use.
            */
            void insert (const iterator& pos, size_type num, param_type val, WAtomic& at)
            {
               if (0 == num)
               {
                  return;
               }

               auto vals = SingleValue (val, num);
               InsertNodes (vals, pos.m_node_p, at);
            }

            /**
               Erases the given element.

               @param it Iterator poingtin at the element to remove.

               @param at The transaction to use.
            */
            iterator erase (const iterator& pos, WAtomic& at)
            {
               if (!pos.m_node_p)
               {
                  return iterator ();
               }
               return iterator (
                  EraseNodes (pos.m_node_p, pos.m_node_p->m_next_v.Get (at), 1, at), at);
            }
			
            /**
               Erases the given range of elements.

               @param start The start of the range to erase.

               @param start The end of the range to erase.

               @param at The transaction to use.
            */
            iterator erase (const iterator& start, const iterator& end, WAtomic& at)
            {
               if (!start.m_node_p || start == end)
               {
                  return start;
               }
               return iterator (EraseNodes (start.m_node_p, end.m_node_p,
                                            CountNodes (start.m_node_p, end.m_node_p, at), at), at);
            }

            //!{
            /**
               Erases all elements in the list.

               @param at The transaction to use, if not provided a new
               transaction will be used.
            */
            void clear ()
            {
               Atomically (boost::bind (&WSTMList::ClearAt, this, _1));
            }
			
            void clear (WAtomic& at)
            {
               ClearAt (at);
            }
            //!}

            //!{
            /**
               Resizes the list.

               @param at The transaction to use, if not provided a new
               transaction will be used.

               @param num The new size for the list.

               @param val If the list size is increasing the new
               elements at the end of the list will ho0ld copies of
               this value.
            */
            void resize (WAtomic& at, size_type num, param_type val = value_type ())
            {
               ResizeAt (num, val, at);
            }

            void resize (size_type num, param_type val = value_type ())
            {
               Atomically (boost::bind (&WSTMList::ResizeAt, this, num,
                                        STLUtils::WrapIfRef<param_type>::wrap (val), _1));
            }
            //!}
			
            /**
               Copies the contents of the given list into this one.

               @param list The list to copy.

               @param at The transaction to use.
            */
            void copy (const WSTMList& list, WAtomic& at)
            {
               ClearAt (at);
               auto vals = IterValues<WSTMList::const_iterator> (list.begin (at), list.end (at));
               InsertNodes (vals, m_empty_p, at);
            }

            /**
               Copies the contents of the given list into this one.

               @param list The list to copy.
            */
            WSTMList& operator= (const WSTMList& list)
            {
               Atomically (boost::bind (&WSTMList::copy, this, boost::cref (list), _1));
               return *this;
            }
			
            /**
               Checks this list for element-wise equality with the
               given list.

               @param list The list to test for equality.

               @return true if the two lists are the same size and
               corresponding elements from each list have equal
               values, false otherwise.
            */
            bool operator== (const WSTMList& list) const
            {
               return Atomically (boost::bind (&WSTMList::IsEqual, this, boost::cref (list), _1));
            }
			
            /**
               This is the opposite of operator==.
            */
            bool operator!= (const WSTMList& list) const
            {
               return ! (*this == list);
            }

            /**
               Does a lexigraphical comaprison of this list and the
               given list.

               @param list The list to compare this list to.

               @return True if this list has an element with value
               less than the value of the corresponding element in the
               other list and the preceding elements of the lists have
               equal values.  If this first set of corresponding
               non-equal elements has the element from the second list
               smaller than false is returned.  If all of the elements
               of the first list equal the corresponding elements
               from the second list and the second list is longer than
               true is returned.  Otherwise false is returned.
            */
            bool operator< (const WSTMList& list) const
            {
               return Atomically (
                  boost::bind (&WSTMList::IsLessThan, this, boost::cref (list), _1));
            }
			
            struct WNode
            {
               typedef boost::shared_ptr<WNode> Ptr;
               typedef WVar<Ptr> Var;
               typedef boost::weak_ptr<WNode> WPtr;
               typedef WVar<WPtr> WeakVar;

               WNode (param_type val, const Ptr& prev_p):
                  m_val (val), m_prev_v (WPtr (prev_p))
               {}
				
               Value_t m_val;
               WeakVar m_prev_v;
               Var m_next_v;
            };

         private:

            typedef typename WNode::Ptr NodePtr;
            const NodePtr m_empty_p;
            typename WNode::Var m_head_v;
            typename WNode::Var m_tail_v;
            WVar<int> m_size_v;
			
            bool EmptyAt (WAtomic& at) const
            {
               return !m_head_v.Get (at);
            }

            size_type SizeAt (WAtomic& at) const
            {
               return m_size_v.Get (at);
            }

            void PushFrontAt (param_type val, WAtomic& at)
            {
               NodePtr head_p = m_head_v.Get (at);
               auto vals = SingleValue (val, 1);
               InsertNodes (vals, head_p, at);
            }

            void PushBackAt (param_type val, WAtomic& at)
            {
               SingleValue sv (val, 1);
               InsertNodes (sv, m_empty_p, at);
            }

            void PopFrontAt (WAtomic& at)
            {
               NodePtr head_p = m_head_v.Get (at);
               EraseNodes (head_p, head_p->m_next_v.Get (at), 1, at);
            }
			
            void PopBackAt (WAtomic& at)
            {
               NodePtr tail_p = m_tail_v.Get (at);
               EraseNodes (tail_p, m_empty_p, 1, at);
            }

            void SwapAt (WSTMList& list, WAtomic& at)
            {
               NodePtr head_p = m_head_v.Get (at);
               NodePtr tail_p = m_tail_v.Get (at);
               const size_type curSize = m_size_v.Get (at);
               m_head_v.Set (list.m_head_v.Get (at), at);
               m_tail_v.Set (list.m_tail_v.Get (at), at);
               m_size_v.Set (list.m_size_v.Get (at), at);
               list.m_head_v.Set (head_p, at);
               list.m_tail_v.Set (tail_p, at);
               list.m_size_v.Set (curSize, at);
            }

            reference FrontAt (WAtomic& at)
            {
               NodePtr head_p = m_head_v.Get (at);
               if (!head_p)
               {
                  throw WSTMListOutOfBoundsError ();
               }
               return head_p->m_val;
            }
			
            param_type ConstFrontAt (WAtomic& at)
            {
               NodePtr head_p = m_head_v.Get (at);
               if (!head_p)
               {
                  throw WSTMListOutOfBoundsError ();
               }
               return head_p->m_val;
            }
			
            reference BackAt (WAtomic& at)
            {
               NodePtr tail_p = m_tail_v.Get (at);
               if (!tail_p)
               {
                  throw WSTMListOutOfBoundsError ();
               }
               return tail_p->m_val;
            }
			
            param_type ConstBackAt (WAtomic& at)
            {
               NodePtr tail_p = m_tail_v.Get (at);
               if (!tail_p)
               {
                  throw WSTMListOutOfBoundsError ();
               }
               return tail_p->m_val;
            }

            struct SingleValue
            {
               SingleValue (param_type val, const size_type numRepeats):
                  m_val (val), m_numRepeats (numRepeats), m_repeats (0)
               {}

               param_type m_val;
               const size_type m_numRepeats;
               size_type m_repeats;

               param_type operator* () const
               {
                  return m_val;
               }

               void next ()
               {
                  ++m_repeats;
               }

               bool hasNext () const
               {
                  return m_repeats < m_numRepeats;
               }

               size_type num () const
               {
                  return m_numRepeats;
               }

            private:
               SingleValue& operator= (const SingleValue&) { return *this; }
            };

            template <typename Iter_t>
            struct IterValues
            {
               IterValues (Iter_t it, Iter_t end):
                  m_it (it), m_end (end), m_count (0)
               {}

               Iter_t m_it;
               Iter_t m_end;
               size_type m_count;
				
               param_type operator* () const
               {
                  return *m_it;
               }

               void next ()
               {
                  ++m_it;
                  ++m_count;
               }

               bool hasNext () const
               {
                  return m_it != m_end;
               }

               size_type num () const
               {
                  return m_count;
               }
            };


            template <typename Values_t>
            NodePtr InsertNodes (Values_t& vals,
                                 const NodePtr& next_p,
                                 WAtomic& at)
            {
               NodePtr prev_p = (next_p ?
                                 next_p->m_prev_v.Get (at).lock () :
                                 m_tail_v.Get (at));
               NodePtr last_p = prev_p;
               NodePtr first_p (new WNode (*vals, last_p));
               if (last_p)
               {
                  last_p->m_next_v.Set (first_p, at);
               }
               last_p = first_p;
               if (!prev_p)
               {
                  m_head_v.Set (first_p, at);
               }
               vals.next ();
               while (vals.hasNext ())
               {
                  NodePtr node_p (new WNode (*vals, last_p));
                  if (last_p)
                  {
                     last_p->m_next_v.Set (node_p, at);
                  }
                  last_p = node_p;
                  vals.next ();
               }
				
               if (next_p)
               {
                  last_p->m_next_v.Set (next_p, at);
                  next_p->m_prev_v.Set (last_p, at);
               }
               else
               {
                  m_tail_v.Set (last_p, at);
               }

               m_size_v.Set (m_size_v.Get (at) + static_cast<int> (vals.num ()), at);

               return first_p;
            }
							 
            NodePtr EraseNodes (NodePtr begin_p,
                                NodePtr end_p,
                                const size_type numNodes,
                                WAtomic& at)
            {
               NodePtr prev_p = begin_p->m_prev_v.Get (at).lock ();
               if (prev_p)
               {
                  prev_p->m_next_v.Set (end_p, at);
               }
               else
               {
                  m_head_v.Set (end_p, at);
               }
               if (end_p)
               {
                  end_p->m_prev_v.Set (prev_p, at);
               }
               else
               {
                  m_tail_v.Set (prev_p, at);
               }
               m_size_v.Set (m_size_v.Get (at) - static_cast<int> (numNodes), at);
               return end_p;
            }

            size_type CountNodes (NodePtr begin_p, NodePtr end_p, WAtomic& at)
            {
               size_type count = 0;
               while (begin_p != end_p)
               {
                  begin_p = begin_p->m_next_v.Get (at);
                  ++count;
               }
               return count;
            }			

            void ClearAt (WAtomic& at)
            {
               m_head_v.Set (m_empty_p, at);
               m_tail_v.Set (m_empty_p, at);
               m_size_v.Set (0, at);
            }

            //Finds the n'th node (zero indexed)
            NodePtr NthNode (size_type n, WAtomic& at)
            {
               NodePtr node_p = m_head_v.Get (at);
               while (n > 0)
               {
                  node_p = node_p->m_next_v.Get (at);
                  --n;
               }
               return node_p;
            }
			
            void ResizeAt (const size_type num, param_type val, WAtomic& at)
            {
               const size_type curSize = m_size_v.Get (at);
               if (num < curSize)
               {
                  EraseNodes (NthNode (num, at), m_empty_p, curSize - num, at);
               }
               else if (num > curSize)
               {
                  auto vals = SingleValue (val, num - curSize);
                  InsertNodes (vals, m_empty_p, at);
               }
            }

            bool IsEqual (const WSTMList& list, WAtomic& at) const
            {
               const size_type s1 = m_size_v.Get (at);
               const size_type s2 = list.m_size_v.Get (at);
               if (s1 != s2)
               {
                  return false;
               }
               if (0 == s1)
               {
                  return true;
               }

               NodePtr node1_p = m_head_v.Get (at);
               NodePtr node2_p = list.m_head_v.Get (at);
               while (node1_p)
               {
                  if (node1_p->m_val != node2_p->m_val)
                  {
                     return false;
                  }
                  node1_p = node1_p->m_next_v.Get (at);
                  node2_p = node2_p->m_next_v.Get (at);
               }
               return true;
            }

            bool IsLessThan (const WSTMList& list, WAtomic& at) const
            {
               NodePtr node1_p = m_head_v.Get (at);
               NodePtr node2_p = list.m_head_v.Get (at);
               while (node1_p && node2_p)
               {
                  if (node1_p->m_val < node2_p->m_val)
                  {
                     return true;
                  }
                  else if (node1_p->m_val > node2_p->m_val)
                  {
                     return false;
                  }

                  node1_p = node1_p->m_next_v.Get (at);
                  node2_p = node2_p->m_next_v.Get (at);
               }
               return node2_p; //the list is longer than we are if
               //node2_p is valid
            }
			
         };
      }

	}
}

