/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#define TEST_MODE
#include "persistent_list.h"
using namespace  WSTM;

// #pragma warning (push)
// #pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/test/unit_test.hpp>
// #pragma warning (pop)

BOOST_AUTO_TEST_SUITE (PersistentList)

namespace
{
	using IList = WPersistentList<int>;

   template <typename Iter_t, typename Dist_t>
   Iter_t Advance (const Iter_t start, const Dist_t d)
   {
      auto it = start;
      std::advance (it, d);
      return it;
   }
}
			
BOOST_AUTO_TEST_CASE (test_defaultConstructor)
{
	IList list;
	BOOST_CHECK (list.empty ());
	BOOST_CHECK_EQUAL (static_cast<size_t>(0), list.size ());
}

BOOST_AUTO_TEST_CASE (test_typedefs)
{
	BOOST_CHECK ((std::is_same<int, IList::value_type>::value));
}

BOOST_AUTO_TEST_CASE (test_push_front)
{
	IList list;
	IList list2 = list;
	list2.push_front(0);
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());
	BOOST_CHECK(!list2.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	IList list3 = push_front(1, list2);
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());
	BOOST_CHECK(!list2.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	BOOST_CHECK(!list3.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list3.size());				
}

BOOST_AUTO_TEST_CASE (test_front)
{
	IList list;
	list.push_front(0);
	BOOST_CHECK_EQUAL(0, list.front());
	IList list2 = list;
	list2.push_front(1);
	BOOST_CHECK_EQUAL(0, list.front());
	BOOST_CHECK_EQUAL(1, list2.front());
	IList list3;
	BOOST_CHECK_THROW(list3.front(), WNoElementError);
}

BOOST_AUTO_TEST_CASE (test_pop_front)
{
	IList list;
	list.push_front(0);
	list.push_front(1);
	IList list2 = pop_front(list);
	IList list3 = list2;
	list3.pop_front();
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	BOOST_CHECK_EQUAL(1, list.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	BOOST_CHECK_EQUAL(0, list2.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list3.size());
	IList list4 = pop_front(list3);
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list3.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list4.size());
}

BOOST_AUTO_TEST_CASE (test_splitHead)
{
	IList list;
	list.push_front(0);
	list.push_front(1);
	list.push_front(2);
	int head1 = -1;
	IList tail1;
   std::tie(head1, tail1) = splitHead(list);
	BOOST_CHECK_EQUAL(2, head1);
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), list.size());
	BOOST_CHECK_EQUAL(2, list.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), tail1.size());
	BOOST_CHECK_EQUAL(1, tail1.front());

	int head2 = -1;
	IList tail2;
   std::tie(head2, tail2) = splitHead(tail1);
	BOOST_CHECK_EQUAL(1, head2);
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), list.size());
	BOOST_CHECK_EQUAL(2, list.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), tail1.size());
	BOOST_CHECK_EQUAL(1, tail1.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), tail2.size());
	BOOST_CHECK_EQUAL(0, tail2.front());

	int head3 = -1;
	IList tail3;
   std::tie(head3, tail3) = splitHead(tail2);
	BOOST_CHECK_EQUAL(0, head3);
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), list.size());
	BOOST_CHECK_EQUAL(2, list.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), tail1.size());
	BOOST_CHECK_EQUAL(1, tail1.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), tail2.size());
	BOOST_CHECK_EQUAL(0, tail2.front());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), tail3.size());

	BOOST_CHECK_THROW(splitHead(tail3), WNoElementError);
}

BOOST_AUTO_TEST_CASE (test_push_back)
{
	IList list;
	IList list2 = list;
	list2.push_back(0);
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());
	BOOST_CHECK(!list2.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	IList list3 = push_back(1, list2);
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());
	BOOST_CHECK(!list2.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	BOOST_CHECK(!list3.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list3.size());
}

BOOST_AUTO_TEST_CASE (test_back)
{
	IList list;
	list.push_back(0);
	BOOST_CHECK_EQUAL(0, list.back());
	IList list2 = list;
	list2.push_back(1);
	BOOST_CHECK_EQUAL(0, list.back());
	BOOST_CHECK_EQUAL(0, list2.front());
	BOOST_CHECK_EQUAL(1, list2.back());
	IList list3;
	BOOST_CHECK_THROW(list3.back(), WNoElementError);
}

BOOST_AUTO_TEST_CASE (test_pop_back)
{
	IList list;
	list.push_back(0);
	list.push_back(1);
	IList list2 = pop_back(list);
	IList list3 = list2;
	list3.pop_back();
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	BOOST_CHECK_EQUAL(1, list.back());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list2.size());
	BOOST_CHECK_EQUAL(0, list2.back());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list3.size());
	IList list4 = pop_back(list3);
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list3.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list4.size());
}

BOOST_AUTO_TEST_CASE (test_concat)
{
	IList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	IList list2;
	list2.push_back(4);
	list2.push_back(5);
	list2.push_back(6);
	IList list3 = list1;
	list3.concat(list2);
	BOOST_CHECK_EQUAL(static_cast<size_t>(4), list1.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), list2.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(7), list3.size());
	for(int i  = 0; i < 7; ++i)
	{
		BOOST_CHECK_EQUAL(i, list3.front());
		list3.pop_front();
	}

	IList list4 = list1 + list2;
	BOOST_CHECK_EQUAL(static_cast<size_t>(4), list1.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), list2.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(7), list4.size());
	for(int i  = 0; i < 7; ++i)
	{
		BOOST_CHECK_EQUAL(i, list4.front());
		list4.pop_front();
	}
}

namespace
{
	struct TestObj
	{
		int val;
		TestObj(int v) : val(v){}
		bool operator==(const TestObj& o) const {return val == o.val;}
	};

	std::ostream& operator<<(std::ostream& out, const TestObj& obj)
	{
		out << "TestObj(" << obj.val << ")";
		return out;
	}
	typedef WPersistentList<TestObj> OList;
}

BOOST_AUTO_TEST_CASE (test_iterator)
{
	OList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	OList::iterator begin = list1.begin();
	OList::iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::iterator end = list1.end();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);

	while(it != begin)
	{
		--it;
		--count;
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);					
	}
	BOOST_CHECK_EQUAL(0, count);

	it = begin;
	BOOST_CHECK_THROW(--it, WInvalidIteratorError);
	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	//normally one would not modfiy a value in a
	//persistent list like this but we need to test
	//modification through an iterator
	it = begin;
	while(it != end)
	{
		it->val += 2;
		++it;
	}
	it = begin;
	int i = 2;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(i, it->val);
		++i;
		++it;
	}
}

BOOST_AUTO_TEST_CASE (test_const_iterator)
{
	OList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	const OList& clist = list1;
	OList::const_iterator begin = clist.begin();
	OList::const_iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::const_iterator end = clist.end();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);

	while(it != begin)
	{
		--it;
		--count;
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);					
	}
	BOOST_CHECK_EQUAL(0, count);

	it = begin;
	BOOST_CHECK_THROW(--it, WInvalidIteratorError);
	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	//test conversion from iterator and equality
	OList::iterator it2 = list1.begin();
	OList::const_iterator cit2 = list1.begin();
	OList::iterator end2 = list1.end();
	OList::const_iterator cend2 = list1.end();
	BOOST_CHECK(it2 == cit2);
	BOOST_CHECK(cit2 == it2);
	BOOST_CHECK(end2 == cend2);
	BOOST_CHECK(cend2 == end2);
	BOOST_CHECK(it2 != cend2);
	BOOST_CHECK(cend2 != it2);
	BOOST_CHECK(cit2 != end2);
	BOOST_CHECK(end2 != cit2);
}

BOOST_AUTO_TEST_CASE (test_equality)
{
	IList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	IList list2 = list1;
	IList list3 = pop_back(list2);
	IList list4;
	list4.push_back(0);
	list4.push_back(1);
	list4.push_back(2);
	list4.push_back(3);
	IList list5 = list4;
	IList list6 = pop_back(list5);
				
	BOOST_CHECK(list1 == list2);
	BOOST_CHECK(list1 == list4);
	BOOST_CHECK(list2 == list4);

	BOOST_CHECK(list1 == list2);
	BOOST_CHECK(list1 == list5);
	BOOST_CHECK(list2 == list5);

	BOOST_CHECK(list3 == list6);
				
	BOOST_CHECK(!(list1 == list3));
	BOOST_CHECK(!(list1 == list6));
	BOOST_CHECK(!(list2 == list3));
	BOOST_CHECK(!(list2 == list6));
	BOOST_CHECK(!(list4 == list3));
	BOOST_CHECK(!(list4 == list6));
	BOOST_CHECK(!(list5 == list3));
	BOOST_CHECK(!(list5 == list6));

				
	BOOST_CHECK(!(list1 != list2));
	BOOST_CHECK(!(list1 != list4));
	BOOST_CHECK(!(list2 != list4));

	BOOST_CHECK(!(list1 != list2));
	BOOST_CHECK(!(list1 != list5));
	BOOST_CHECK(!(list2 != list5));

	BOOST_CHECK(!(list3 != list6));
				
	BOOST_CHECK(list1 != list3);
	BOOST_CHECK(list1 != list6);
	BOOST_CHECK(list2 != list3);
	BOOST_CHECK(list2 != list6);
	BOOST_CHECK(list4 != list3);
	BOOST_CHECK(list4 != list6);
	BOOST_CHECK(list5 != list3);
	BOOST_CHECK(list5 != list6);
}

BOOST_AUTO_TEST_CASE (test_reverse_iterator)
{
	OList list1;
	list1.push_front(0);
	list1.push_front(1);
	list1.push_front(2);
	list1.push_front(3);
	OList::reverse_iterator begin = list1.rbegin();
	OList::reverse_iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::reverse_iterator end = list1.rend();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);

	while(it != begin)
	{
		--it;
		--count;
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);					
	}
	BOOST_CHECK_EQUAL(0, count);

	it = begin;
	BOOST_CHECK_THROW(--it, WInvalidIteratorError);
	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	it = end;
	OList::iterator cit = it.base();
	BOOST_CHECK_EQUAL(3, cit->val);

	//normally one would not modfiy a value in a
	//persistent list like this but we need to test
	//modification through an iterator
	it = begin;
	while(it != end)
	{
		it->val += 2;
		++it;
	}
	it = begin;
	int i = 2;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(i, it->val);
		++i;
		++it;
	}
}

BOOST_AUTO_TEST_CASE (test_const_reverse_iterator)
{
	OList list1;
	list1.push_front(0);
	list1.push_front(1);
	list1.push_front(2);
	list1.push_front(3);
	const OList& clist = list1;
	OList::const_reverse_iterator begin = clist.rbegin();
	OList::const_reverse_iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::const_reverse_iterator end = clist.rend();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);

	while(it != begin)
	{
		--it;
		--count;
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);					
	}
	BOOST_CHECK_EQUAL(0, count);

	it = begin;
	BOOST_CHECK_THROW(--it, WInvalidIteratorError);
	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	it = end;
	OList::const_iterator cit = it.base();
	BOOST_CHECK_EQUAL(3, cit->val);

	//test conversion from iterator and equality
	OList::reverse_iterator it2 = list1.rbegin();
	OList::const_reverse_iterator cit2 = list1.rbegin();
	OList::reverse_iterator end2 = list1.rend();
	OList::const_reverse_iterator cend2 = list1.rend();
	BOOST_CHECK(it2 == cit2);
	BOOST_CHECK(cit2 == it2);
	BOOST_CHECK(end2 == cend2);
	BOOST_CHECK(cend2 == end2);
	BOOST_CHECK(it2 != cend2);
	BOOST_CHECK(cend2 != it2);
	BOOST_CHECK(cit2 != end2);
	BOOST_CHECK(end2 != cit2);
}

BOOST_AUTO_TEST_CASE (test_insert)
{
	IList list;
	IList old = list;
	IList::iterator it1 = list.insert(list.begin(), 1);
	BOOST_CHECK(old.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), list.size());
	BOOST_CHECK_EQUAL(1, list.front());
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK(list.checkIter(it1));
				
	IList old1 = list;
	IList::iterator it2 = list.insert(list.begin(), 0);
	BOOST_CHECK_EQUAL(static_cast<size_t>(1), old1.size());
	BOOST_CHECK_EQUAL(1, old1.front());
	IList exp1 = push_front(0, push_front(1, IList()));
	BOOST_CHECK_EQUAL(exp1, list);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(0, *it2);
	BOOST_CHECK(list.checkIter(it2));
	BOOST_CHECK(!list.checkIter(it1));
	BOOST_CHECK(old1.checkIter(it1));

	IList old2 = list;
	IList::iterator it3 = list.insert(list.end(), 3);
	BOOST_CHECK_EQUAL(exp1, old2);
	IList exp2 = push_front(0, push_front(1, push_front(3, IList())));
	BOOST_CHECK_EQUAL(exp2, list);
	BOOST_CHECK_EQUAL(3, *it3);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(0, *it2);
	BOOST_CHECK(list.checkIter(it3));
	BOOST_CHECK(!list.checkIter(it2));
	BOOST_CHECK(old2.checkIter(it2));
	BOOST_CHECK(old1.checkIter(it1));

	IList old3 = list;
	IList::iterator it4 = list.insert(Advance (std::begin (list), 2), 2);
	BOOST_CHECK_EQUAL(static_cast<size_t>(3), old3.size());
	BOOST_CHECK_EQUAL(exp2, old3);				
	IList exp3 = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp3, list);
	BOOST_CHECK_EQUAL(2, *it4);
	BOOST_CHECK_EQUAL(3, *it3);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(0, *it2);
	BOOST_CHECK(list.checkIter(it4));
	BOOST_CHECK(!list.checkIter(it3));
	BOOST_CHECK(old3.checkIter(it3));
	BOOST_CHECK(old2.checkIter(it2));
	BOOST_CHECK(old1.checkIter(it1));

	IList list2 = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
   BOOST_CHECK_THROW(list2.insert(Advance (std::begin (list), 2), 5), WInvalidIteratorError);
}

BOOST_AUTO_TEST_CASE (test_replace)
{
	IList list = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
	IList old1 = list;
	IList::iterator it1 = list.replace(list.begin(), 1);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK(list.checkIter(it1));
	IList expOld1 = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(expOld1, old1);
	IList exp1 = push_front(1, push_front(1, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp1, list);

	IList old2 = list;
	IList::iterator it2 = list.replace(Advance (std::begin (list), 1), 2);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(2, *it2);				
	BOOST_CHECK(list.checkIter(it2));
	BOOST_CHECK(!list.checkIter(it1));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp1, old2);
	IList exp2 = push_front(1, push_front(2, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp2, list);
				
	IList old3 = list;
	IList::iterator it3 = list.replace(Advance (std::begin (list), 2), 3);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(2, *it2);				
	BOOST_CHECK_EQUAL(3, *it3);				
	BOOST_CHECK(list.checkIter(it3));
	BOOST_CHECK(!list.checkIter(it2));
	BOOST_CHECK(old3.checkIter(it2));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp2, old3);
	IList exp3 = push_front(1, push_front(2, push_front(3, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp3, list);
				
	IList old4 = list;
	IList::iterator it4 = list.replace(Advance (std::begin (list), 3), 4);
	BOOST_CHECK_EQUAL(1, *it1);
	BOOST_CHECK_EQUAL(2, *it2);				
	BOOST_CHECK_EQUAL(3, *it3);				
	BOOST_CHECK_EQUAL(4, *it4);				
	BOOST_CHECK(list.checkIter(it4));
	BOOST_CHECK(!list.checkIter(it3));
	BOOST_CHECK(old4.checkIter(it3));
	BOOST_CHECK(old3.checkIter(it2));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp3, old4);
	IList exp4 = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	BOOST_CHECK_EQUAL(exp4, list);

	BOOST_CHECK_THROW(list.replace(++exp4.begin(), 5), WInvalidIteratorError);
}

BOOST_AUTO_TEST_CASE (test_clear)
{
	IList list = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	list.clear();
	BOOST_CHECK(list.empty());
}

BOOST_AUTO_TEST_CASE (test_forward_iterator)
{
	OList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	OList::forward_iterator begin = list1.fbegin();
	OList::forward_iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::forward_iterator end = list1.fend();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);
   
	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	//normally one would not modfiy a value in a
	//persistent list like this but we need to test
	//modification through an iterator
	it = begin;
	while(it != end)
	{
		it->val += 2;
		++it;
	}
	it = begin;
	int i = 2;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(i, it->val);
		++i;
		++it;
	}
}

BOOST_AUTO_TEST_CASE (test_const_forward_iterator)
{
	OList list1;
	list1.push_back(0);
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	const OList& clist = list1;
	OList::const_forward_iterator begin = clist.fbegin();
	OList::const_forward_iterator it = begin;
	BOOST_CHECK(it == begin);
	OList::const_forward_iterator end = clist.fend();
	BOOST_CHECK(it != end);
	list1.push_back(4);
	int count = 0;
	while(it != end)
	{
		BOOST_CHECK_EQUAL(count, (*it).val);
		BOOST_CHECK_EQUAL(count, it->val);
		++count;
		++it;
	}
	BOOST_CHECK_EQUAL(4, count);

	it = end;
	BOOST_CHECK_THROW(++it, WInvalidIteratorError);

	//test conversion from iterator and equality
	OList::forward_iterator it2 = list1.fbegin();
	OList::const_forward_iterator cit2 = list1.fbegin();
	OList::forward_iterator end2 = list1.fend();
	OList::const_forward_iterator cend2 = list1.fend();
	BOOST_CHECK(it2 == cit2);
	BOOST_CHECK(cit2 == it2);
	BOOST_CHECK(end2 == cend2);
	BOOST_CHECK(cend2 == end2);
	BOOST_CHECK(it2 != cend2);
	BOOST_CHECK(cend2 != it2);
	BOOST_CHECK(cit2 != end2);
	BOOST_CHECK(end2 != cit2);
}

BOOST_AUTO_TEST_CASE (test_erase)
{
	IList list = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	IList old1 = list;
	IList::iterator it1 = list.erase(Advance (std::begin (list), 2));
	BOOST_CHECK_EQUAL(4, *it1);
	BOOST_CHECK(list.checkIter(it1));
	IList oldExp = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	BOOST_CHECK_EQUAL(oldExp, old1);
	IList exp1 = push_front(1, push_front(2, push_front(4, IList())));
	BOOST_CHECK_EQUAL(exp1, list);
				
	IList old2 = list;
	IList::iterator it2 = list.erase(list.begin());
	BOOST_CHECK_EQUAL(2, *it2);
	BOOST_CHECK(list.checkIter(it2));
	BOOST_CHECK(!list.checkIter(it1));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp1, old2);
	IList exp2 = push_front(2, push_front(4, IList()));
	BOOST_CHECK_EQUAL(exp2, list);
				
	IList old3 = list;
   auto eraseIt = std::end (list);
   --eraseIt;
	IList::iterator it3 = list.erase(eraseIt);
	BOOST_CHECK(it3 == list.end());
	BOOST_CHECK(list.checkIter(it3));
	BOOST_CHECK(!list.checkIter(it2));
	BOOST_CHECK(old3.checkIter(it2));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp2, old3);
	IList exp3 = push_front(2, IList());
	BOOST_CHECK_EQUAL(exp3, list);
	BOOST_CHECK_THROW(list.erase(list.end()), WInvalidIteratorError);
				
	IList old4 = list;
	IList::iterator it4 = list.erase(list.begin());
	BOOST_CHECK(it4 == list.end());
	BOOST_CHECK(list.checkIter(it4));
	BOOST_CHECK(!list.checkIter(it3));
	BOOST_CHECK(old4.checkIter(it3));
	BOOST_CHECK(old3.checkIter(it2));
	BOOST_CHECK(old2.checkIter(it1));
	BOOST_CHECK_EQUAL(exp3, old4);
	BOOST_CHECK(list.empty());

	BOOST_CHECK_THROW(list.erase(list.begin()), WInvalidIteratorError);
	BOOST_CHECK_THROW(exp1.erase(++exp2.begin()), WInvalidIteratorError);
}

BOOST_AUTO_TEST_CASE (test_assignment)
{
	IList list = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	IList list2;
	list2 = list;
	BOOST_CHECK_EQUAL(list, list2);
}

BOOST_AUTO_TEST_CASE (test_rangeCtor)
{
	std::vector<int> vec1;
	vec1.push_back(0);
	vec1.push_back(1);
	vec1.push_back(2);
	vec1.push_back(3);
	IList list1(vec1.begin(), vec1.end());
	IList exp1 = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp1, list1);

	std::vector<int> vec2;
	IList list2(vec2.begin(), vec2.end());
	BOOST_CHECK(list2.empty());
}

BOOST_AUTO_TEST_CASE (test_rangeInsert)
{
	std::vector<int> vec1;
	vec1.push_back(0);
	vec1.push_back(1);
	vec1.push_back(2);
	vec1.push_back(3);
	IList list1;
	IList old1 = list1;
	list1.insert(list1.begin(), vec1.begin(), vec1.end());
	IList exp1 = push_front(0, push_front(1, push_front(2, push_front(3, IList()))));
	BOOST_CHECK_EQUAL(exp1, list1);
	BOOST_CHECK(old1.empty());
				
	std::vector<int> vec2;
	IList list2(vec2.begin(), vec2.end());
	BOOST_CHECK(list2.empty());

	IList list3 = push_front(-1, push_front(4, IList()));
	IList old3 = list3;
	list3.insert(++list3.begin(), vec1.begin(), vec1.end());
	IList exp3 = push_front(-1, push_front(0, push_front(1, push_front(2, push_front(3, push_front(4, IList()))))));
	IList oldExp3 = push_front(-1, push_front(4, IList()));
	BOOST_CHECK_EQUAL(exp3, list3);
	BOOST_CHECK_EQUAL(oldExp3, old3);

	IList list4 = old3;
	list4.insert(list4.begin(), vec1.begin(), vec1.end());
	IList exp4 = push_front(0, push_front(1, push_front(2, push_front(3, push_front(-1, push_front(4, IList()))))));
	BOOST_CHECK_EQUAL(exp4, list4);
	BOOST_CHECK_EQUAL(oldExp3, old3);

	IList list5 = old3;
	list5.insert(list5.end(), vec1.begin(), vec1.end());
	IList exp5 = push_front(-1, push_front(4, push_front(0, push_front(1, push_front(2, push_front(3, IList()))))));
	BOOST_CHECK_EQUAL(exp5, list5);
	BOOST_CHECK_EQUAL(oldExp3, old3);

	IList listExc;
	BOOST_CHECK_THROW(listExc.insert(++list3.begin(), vec1.begin(), vec1.end()), WInvalidIteratorError);
}

BOOST_AUTO_TEST_CASE (test_rangeErase)
{
	IList list = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	IList initial = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	IList old = list;
	IList::iterator it1 = list.erase(list.begin(), list.end());
	BOOST_CHECK(list.checkIter(it1));
	BOOST_CHECK(!old.checkIter(it1));
	BOOST_CHECK(it1 == list.end());
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(initial, old);

	list = old;
	IList::iterator it2 = list.erase(Advance (std::begin (list), 1), Advance (std::begin (list), 3)); 
	IList exp1 = push_front(1, push_front(4, IList()));
	BOOST_CHECK(list.checkIter(it2));
	BOOST_CHECK(!old.checkIter(it2));
	BOOST_CHECK_EQUAL(4, *it2);
	BOOST_CHECK_EQUAL(exp1, list);
	BOOST_CHECK_EQUAL(initial, old);

	list = old;
	IList::iterator it3 = list.erase(Advance (std::begin (list), 1), list.end());
	BOOST_CHECK(list.checkIter(it3));
	BOOST_CHECK(!old.checkIter(it3));
	BOOST_CHECK(it3 == list.end());
	IList exp2 = push_front(1, IList());
	BOOST_CHECK_EQUAL(exp2, list);
	BOOST_CHECK_EQUAL(initial, old);
				
	list = old;
	IList::iterator it4 = list.erase(list.begin(), Advance (std::begin (list), 3));
	BOOST_CHECK(list.checkIter(it4));
	BOOST_CHECK(!old.checkIter(it4));
	BOOST_CHECK_EQUAL(4, *it4);
	IList exp3 = push_front(4, IList());
	BOOST_CHECK_EQUAL(exp3, list);
	BOOST_CHECK_EQUAL(initial, old);
}

BOOST_AUTO_TEST_CASE (test_checkIter)
{
	IList list1 = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));
	IList list2 = push_front(1, push_front(2, push_front(3, push_front(4, IList()))));

	BOOST_CHECK(list1.checkIter(list1.begin()));
	BOOST_CHECK(list1.checkIter(Advance (std::begin (list1), 1)));
	BOOST_CHECK(list1.checkIter(Advance (std::begin (list1), 2)));
	BOOST_CHECK(list1.checkIter(Advance (std::begin (list1), 3)));
	BOOST_CHECK(list1.checkIter(list1.end()));

	BOOST_CHECK(!list1.checkIter(list2.begin()));
	BOOST_CHECK(!list1.checkIter(Advance (std::begin (list2), 1)));
	BOOST_CHECK(!list1.checkIter(Advance (std::begin (list2), 2)));
	BOOST_CHECK(!list1.checkIter(Advance (std::begin (list2), 3)));
	BOOST_CHECK(!list1.checkIter(list2.end()));
}

BOOST_AUTO_TEST_SUITE_END (/*PersistentList*/)
