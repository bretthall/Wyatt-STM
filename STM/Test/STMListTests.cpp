/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2013. All rights reserved.
****************************************************************************/

#include "StdAfx.h"

#include "BSS/Thread/STM/STMList.h"
#include "BSS/Thread/STM/stm.h"
using namespace  bss::thread::STM;

#pragma warning (push)
#pragma warning (disable: 4127 4244 4265 4389 4503 4512 4640 6011)
#include <boost/bind.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/type_traits/is_same.hpp>
using boost::is_same;
#pragma warning (pop)

#include <algorithm>

namespace
{
	struct CheckVals
	{
		typedef void result_type;
		typedef WSTMList<int> List;
					
		void operator()(const List& list, const int VAL1, const int VAL2, WAtomic& at) const 
		{
			List::const_iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL2, *it);						
		}
					
	};

}

BOOST_AUTO_TEST_SUITE (STMListTests)

BOOST_AUTO_TEST_CASE (test_typedefs)
{
	typedef WSTMList<int> List;
	BOOST_CHECK((is_same<int, List::value_type>::value));
	BOOST_CHECK((is_same<int*, List::pointer>::value));
	BOOST_CHECK((is_same<int&, List::reference>::value));
	BOOST_CHECK((is_same<const int&, List::const_reference>::value));
	BOOST_CHECK((is_same<size_t, List::size_type>::value));
	BOOST_CHECK((is_same<int, List::difference_type>::value));
}

BOOST_AUTO_TEST_CASE (test_defaultCtor)
{
	typedef WSTMList<int> List;
	List list;
	BOOST_CHECK(list.empty());
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());				
}

BOOST_AUTO_TEST_CASE (test_multiCtor)
{
	typedef WSTMList<int> List;
	const size_t SIZE = 12;
	List list(SIZE);
	BOOST_CHECK(!list.empty());				
	BOOST_CHECK_EQUAL(SIZE, list.size());				
}

BOOST_AUTO_TEST_CASE (test_multiCopyCtor)
{
	typedef WSTMList<int> List;
	const size_t SIZE = 12;
	const int VALUE = 30498;
	List list(SIZE, VALUE);
	BOOST_CHECK(!list.empty());
	BOOST_CHECK_EQUAL(SIZE, list.size());

	struct CheckVals
	{
		typedef void result_type;
		void operator()(const List& list, const int val, WAtomic& at) const
		{
			List::const_iterator it = list.begin(at);
			List::const_iterator end = list.end(at);
			while(it != end)
			{
				BOOST_CHECK_EQUAL(val, *it);
				++it;
			}
		}
	};
	Atomically(boost::bind(CheckVals(), boost::ref(list), VALUE, _1));
}

BOOST_AUTO_TEST_CASE (test_copyCtor)
{
	typedef WSTMList<int> List;
	const size_t SIZE = 13;
	const int VALUE = 49387;
	List listInit(SIZE, VALUE);
	List list(listInit);
	BOOST_CHECK(!list.empty());				
	BOOST_CHECK_EQUAL(SIZE, list.size());
	struct CheckVals
	{
		typedef void result_type;
		void operator()(const List& list, const int val, WAtomic& at) const
		{
			List::const_iterator it = list.begin(at);
			List::const_iterator end = list.end(at);
			while(it != end)
			{
				BOOST_CHECK_EQUAL(val, *it);
				++it;
			}
		}
	};
	Atomically(boost::bind(CheckVals(), boost::ref(list), VALUE, _1));
}

BOOST_AUTO_TEST_CASE (test_beginEnd)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			const size_t SIZE1 = 5;
			const int VALUE1 = 9547;
			List list(SIZE1, VALUE1);
			List::iterator it1 = list.begin(at);
			List::iterator end1 = list.end(at);
			BOOST_CHECK(it1 != end1);
			BOOST_CHECK_EQUAL(static_cast<int>(SIZE1), std::distance(it1, end1));

			const size_t SIZE2 = 2;
			const int VALUE2 = 93047;
			const List clist(SIZE2, VALUE2);
			List::const_iterator it2 = clist.begin(at);
			List::const_iterator end2 = clist.end(at);
			BOOST_CHECK(it2 != end2);
			BOOST_CHECK_EQUAL(static_cast<int>(SIZE2), std::distance(it2, end2));

			List elist;
			List::iterator it3 = elist.begin(at);
			List::iterator end3 = elist.end(at);
			BOOST_CHECK(it3 == end3);
		}
	};

	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_push_front)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 3459;
			list.push_front(VAL1, at);
			const int VAL2 = 45734;
			list.push_front(VAL2, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL2, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL1, *it);
		}
	};
	Atomically(boost::bind(Test(), _1));				

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 38547;
	list.push_front(VAL1);
	const int VAL2 = 5074;
	list.push_front(VAL2);
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	Atomically(boost::bind(CheckVals(), boost::ref(list), VAL2, VAL1, _1));
}

BOOST_AUTO_TEST_CASE (test_push_back)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 390584;
			list.push_back(VAL1, at);
			const int VAL2 = 24;
			list.push_back(VAL2, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL2, *it);
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 30407;
	list.push_back(VAL1);
	const int VAL2 = 47603;
	list.push_back(VAL2);
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	Atomically(boost::bind(CheckVals(), boost::ref(list), VAL1, VAL2, _1));
}

BOOST_AUTO_TEST_CASE (test_pop_front)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 93458;
			list.push_back(VAL1, at);
			const int VAL2 = 374;
			list.push_back(VAL2, at);
			const int VAL3 = 3050;
			list.push_back(VAL3, at);
			list.pop_front();
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL2, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL3, *it);
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 3047;
	list.push_back(VAL1);
	const int VAL2 = 173;
	list.push_back(VAL2);
	const int VAL3 = 975;
	list.push_back(VAL3);
	list.pop_front();
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	Atomically(boost::bind(CheckVals(), boost::ref(list), VAL2, VAL3, _1));
}

BOOST_AUTO_TEST_CASE (test_pop_back)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 87345;
			list.push_back(VAL1, at);
			const int VAL2 = 7354;
			list.push_back(VAL2, at);
			const int VAL3 = 65;
			list.push_back(VAL3, at);
			list.pop_back();
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL2, *it);
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 409578;
	list.push_back(VAL1);
	const int VAL2 = 29837;
	list.push_back(VAL2);
	const int VAL3 = 2847;
	list.push_back(VAL3);
	list.pop_back();
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size());
	Atomically(boost::bind(CheckVals(), boost::ref(list), VAL1, VAL2, _1));
}

BOOST_AUTO_TEST_CASE (test_swap)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const 
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 890475;
			list.push_back(VAL1, at);
			const int VAL2 = 234;
			list.push_back(VAL2, at);
			List list2;
			list2.swap(list, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size(at));
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list2.size(at));
			List::iterator it = list2.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *it);
			++it;
			BOOST_CHECK_EQUAL(VAL2, *it);						
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 485;
	list.push_back(VAL1);
	const int VAL2 = 475073;
	list.push_back(VAL2);
	List list2;
	list2.swap(list);
	BOOST_CHECK_EQUAL(static_cast<size_t>(0), list.size());
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), list2.size());
	Atomically(boost::bind(CheckVals(), boost::ref(list2), VAL1, VAL2, _1));
}

BOOST_AUTO_TEST_CASE (test_front)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 9542;
			list.push_back(VAL1, at);
			const int VAL2 = 7302;
			list.push_back(VAL2, at);
			BOOST_CHECK_EQUAL(VAL1, list.front(at));

			List clist;
			const int CVAL1 = 985;
			clist.push_back(CVAL1, at);
			const int CVAL2 = 9785;
			clist.push_back(CVAL2, at);
			BOOST_CHECK_EQUAL(CVAL1, clist.front(at));
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 497;
	list.push_back(VAL1);
	const int VAL2 = 1;
	list.push_back(VAL2);
	BOOST_CHECK_EQUAL(VAL1, list.front());

	List clist;
	const int CVAL1 = 435;
	clist.push_back(CVAL1);
	const int CVAL2 = 3;
	clist.push_back(CVAL2);
	BOOST_CHECK_EQUAL(CVAL1, clist.front());
}

BOOST_AUTO_TEST_CASE (test_back)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 9542;
			list.push_back(VAL1, at);
			const int VAL2 = 7302;
			list.push_back(VAL2, at);
			BOOST_CHECK_EQUAL(VAL2, list.back(at));

			List clist;
			const int CVAL1 = 985;
			clist.push_back(CVAL1, at);
			const int CVAL2 = 9785;
			clist.push_back(CVAL2, at);
			BOOST_CHECK_EQUAL(CVAL2, clist.back(at));
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 497;
	list.push_back(VAL1);
	const int VAL2 = 1;
	list.push_back(VAL2);
	BOOST_CHECK_EQUAL(VAL2, list.back());

	List clist;
	const int CVAL1 = 435;
	clist.push_back(CVAL1);
	const int CVAL2 = 3;
	clist.push_back(CVAL2);
	BOOST_CHECK_EQUAL(CVAL2, clist.back());
}

BOOST_AUTO_TEST_CASE (test_insert)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 89734;
			list.push_back(VAL1, at);
			const int VAL2 = 98745;
			list.push_back(VAL2, at);
			const int VAL3 = 9485;
			List::iterator pos = list.begin(at);
			++pos;
			List::iterator it = list.insert(pos, VAL3, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(3), list.size(at));
			BOOST_CHECK_EQUAL(VAL3, *it);
			List::iterator itM1 = it;
			--itM1;
			BOOST_CHECK_EQUAL(VAL1, *itM1);
			List::iterator itP1 = it;
			++itP1;
			BOOST_CHECK_EQUAL(VAL2, *itP1);
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL3, at);
			clist.push_back(VAL2, at);
			BOOST_CHECK(clist == list);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_insertMultiple)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 3445;
			list.push_back(VAL1, at);
			const int VAL2 = 948563;
			list.push_back(VAL2, at);
			std::vector<int> vals;
			const int VAL3 = 685;
			vals.push_back(VAL3);
			const int VAL4 = 3542;
			vals.push_back(VAL4);
			List::iterator pos = list.begin(at);
			++pos;
			list.insert(pos, vals.begin(), vals.end(), at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(4), list.size(at));
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL3, at);
			clist.push_back(VAL4, at);
			clist.push_back(VAL2, at);
			BOOST_CHECK(clist == list);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_insertMultipleCopies)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 24;
			list.push_back(VAL1, at);
			const int VAL2 = 325;
			list.push_back(VAL2, at);
			const std::vector<int> vals;
			const int VAL3 = 2523;
			List::iterator pos = list.begin(at);
			++pos;
			list.insert(pos, 2, VAL3, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(4), list.size(at));
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL3, at);
			clist.push_back(VAL3, at);
			clist.push_back(VAL2, at);
			BOOST_CHECK(clist == list);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_erase)
{
	static const int VAL1 = 3252;
	static const int VAL2 = 6432;
	static const int VAL3 = 878;
	struct Test
	{
		typedef void result_type;
		void operator()(WSTMList<int>& list, WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List::iterator pos = list.begin(at) + 1;
			List::iterator it = list.erase(pos, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			BOOST_CHECK_EQUAL(VAL3, *it);
			BOOST_CHECK_EQUAL(VAL1, *(it - 1));
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL3, at);
			BOOST_CHECK(clist == list);
		}
	};
	typedef WSTMList<int> List;
	List list;
	list.push_back(VAL1);
	list.push_back(VAL2);
	list.push_back(VAL3);
	Atomically(boost::bind(Test(), boost::ref(list), _1));
}

BOOST_AUTO_TEST_CASE (test_eraseMultiple)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 53412;
			list.push_back(VAL1, at);
			const int VAL2 = 24765;
			list.push_back(VAL2, at);
			const int VAL3 = 35123;
			list.push_back(VAL3, at);
			const int VAL4 = 2733;
			list.push_back(VAL4, at);
			List::iterator pos = list.begin(at);
			++pos;
			List::iterator pos2 = list.begin(at);
			++pos2;
			++pos2;
			++pos2;
			List::iterator it = list.erase(pos, pos2, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			BOOST_CHECK_EQUAL(VAL4, *it);
			List::iterator itM1 = it;
			--itM1;
			BOOST_CHECK_EQUAL(VAL1, *itM1);
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL4, at);
			BOOST_CHECK(clist == list);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_clear)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 904753;
			list.push_back(VAL1, at);
			const int VAL2 = 3244;
			list.push_back(VAL2, at);
			list.clear(at);
			BOOST_CHECK(list.empty(at));
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 40753;
	list.push_back(VAL1);
	const int VAL2 = 4375697;
	list.push_back(VAL2);
	list.clear();
	BOOST_CHECK(list.empty());
}

BOOST_AUTO_TEST_CASE (test_resize)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 97543;
			list.push_back(VAL1, at);
			const int VAL2 = 9372;
			list.push_back(VAL2, at);
			const int VAL3 = 329587;
			list.resize(at, 4, VAL3);
			List clist;
			clist.push_back(VAL1, at);
			clist.push_back(VAL2, at);
			clist.push_back(VAL3, at);
			clist.push_back(VAL3, at);
			BOOST_CHECK(clist == list);
		}
	};
	Atomically(boost::bind(Test(), _1));

	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 3832;
	list.push_back(VAL1);
	const int VAL2 = 8;
	list.push_back(VAL2);
	const int VAL3 = 843831;
	list.resize(4, VAL3);
	List clist;
	clist.push_back(VAL1);
	clist.push_back(VAL2);
	clist.push_back(VAL3);
	clist.push_back(VAL3);
	BOOST_CHECK(clist == list);
}

BOOST_AUTO_TEST_CASE (test_equality)
{
	typedef WSTMList<int> List;
	const int VAL1 = 786553;
	const int VAL2 = 793397;
	const int VAL3 = 901161;
	const int VAL4 = 447504;
	List list1;
	list1.push_back(VAL1);
	list1.push_back(VAL2);
	list1.push_back(VAL3);
	List list2;
	list2.push_back(VAL1);
	list2.push_back(VAL2);
	list2.push_back(VAL3);
	BOOST_CHECK(list1 == list2);
	BOOST_CHECK(!(list1 != list2));
	List list3;
	list3.push_back(VAL1);
	list3.push_back(VAL2);
	list3.push_back(VAL4);
	BOOST_CHECK(list1 != list3);
	BOOST_CHECK(!(list1 == list3));
	List list4;
	list4.push_back(VAL1);
	list4.push_back(VAL2);
	list4.push_back(VAL3);
	list4.push_back(VAL4);
	BOOST_CHECK(list1 != list4);
	BOOST_CHECK(!(list1 == list4));
}

BOOST_AUTO_TEST_CASE (test_lessthan)
{
	typedef WSTMList<int> List;
	List list1;
	list1.push_back(1);
	list1.push_back(2);
	list1.push_back(3);
	BOOST_CHECK(!(list1 < list1));
	List list2;
	list2.push_back(2);
	list2.push_back(3);
	list2.push_back(4);
	BOOST_CHECK(list1 < list2);
	List list3;
	list3.push_back(2);
	list3.push_back(4);
	list3.push_back(5);
	BOOST_CHECK(list1 < list3);
	BOOST_CHECK(list2 < list3);
}

BOOST_AUTO_TEST_CASE (test_copy)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 54421;
			list.push_back(VAL1, at);
			const int VAL2 = 965094;
			list.push_back(VAL2, at);
			List clist;
			const int VAL3 = 719463;
			clist.push_back(VAL3, at);
			clist.copy(list, at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), clist.size(at));
			BOOST_CHECK(list == clist);
		}
	};
	Atomically(boost::bind(Test(), _1));				
}

BOOST_AUTO_TEST_CASE (test_assignment)
{
	typedef WSTMList<int> List;
	List list;
	const int VAL1 = 73884;
	list.push_back(VAL1);
	const int VAL2 = 62805;
	list.push_back(VAL2);
	List clist;
	const int VAL3 = 610141;
	clist.push_back(VAL3);
	clist = list;
	BOOST_CHECK_EQUAL(static_cast<size_t>(2), clist.size());
	BOOST_CHECK(list == clist);
}

BOOST_AUTO_TEST_CASE (test_iterPreInc)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 546466;
			list.push_back(VAL1, at);
			const int VAL2 = 459960;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			++it;
			BOOST_CHECK_EQUAL(VAL2, *it);
			List::const_iterator cit = list.begin(at);
			++cit;
			BOOST_CHECK_EQUAL(VAL2, *cit);
			List::reverse_iterator rit = list.rbegin(at);
			++rit;
			BOOST_CHECK_EQUAL(VAL1, *rit);
			List::const_reverse_iterator crit = list.rbegin(at);
			++crit;
			BOOST_CHECK_EQUAL(VAL1, *crit);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterPostInc)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 92698;
			list.push_back(VAL1, at);
			const int VAL2 = 842340;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			List::iterator it2 = it++;
			BOOST_CHECK_EQUAL(VAL2, *it);
			BOOST_CHECK_EQUAL(VAL1, *it2);
			List::const_iterator cit = list.begin(at);
			List::const_iterator cit2 = cit++;
			BOOST_CHECK_EQUAL(VAL2, *cit);
			BOOST_CHECK_EQUAL(VAL1, *cit2);
			List::reverse_iterator rit = list.rbegin(at);
			List::reverse_iterator rit2 = rit++;
			BOOST_CHECK_EQUAL(VAL1, *rit);
			BOOST_CHECK_EQUAL(VAL2, *rit2);
			List::const_reverse_iterator crit = list.rbegin(at);
			List::const_reverse_iterator crit2 = crit++;
			BOOST_CHECK_EQUAL(VAL1, *crit);
			BOOST_CHECK_EQUAL(VAL2, *crit2);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterPreDec)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 177707;
			list.push_back(VAL1, at);
			const int VAL2 = 86740;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			++it;
			--it;
			BOOST_CHECK_EQUAL(VAL1, *it);
			List::const_iterator cit = list.begin(at);
			++cit;
			--cit;
			BOOST_CHECK_EQUAL(VAL1, *cit);
			List::reverse_iterator rit = list.rbegin(at);
			++rit;
			--rit;
			BOOST_CHECK_EQUAL(VAL2, *rit);
			List::const_reverse_iterator crit = list.rbegin(at);
			++crit;
			--crit;
			BOOST_CHECK_EQUAL(VAL2, *crit);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterPostDec)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 490770;
			list.push_back(VAL1, at);
			const int VAL2 = 711760;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			++it;
			List::iterator it2 = it--;
			BOOST_CHECK_EQUAL(VAL1, *it);
			BOOST_CHECK_EQUAL(VAL2, *it2);
			List::const_iterator cit = list.begin(at);
			++cit;
			List::const_iterator cit2 = cit--;
			BOOST_CHECK_EQUAL(VAL1, *cit);
			BOOST_CHECK_EQUAL(VAL2, *cit2);
			List::reverse_iterator rit = list.rbegin(at);
			++rit;
			List::reverse_iterator rit2 = rit--;
			BOOST_CHECK_EQUAL(VAL2, *rit);
			BOOST_CHECK_EQUAL(VAL1, *rit2);
			List::const_reverse_iterator crit = list.rbegin(at);
			++crit;
			List::const_reverse_iterator crit2 = crit--;
			BOOST_CHECK_EQUAL(VAL2, *crit);
			BOOST_CHECK_EQUAL(VAL1, *crit2);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterEquality)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 191923;
			list.push_back(VAL1, at);
			const int VAL2 = 78121;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			List::iterator it2 = list.begin(at);
			List::iterator end = list.end(at);
			BOOST_CHECK(it == it);
			BOOST_CHECK(it == it2);
			BOOST_CHECK(!(it == end));
			BOOST_CHECK(!(it != it));
			BOOST_CHECK(!(it != it2));
			BOOST_CHECK(it != end);

			const List clist(list);
			List::const_iterator cit = clist.begin(at);
			List::const_iterator cit2 = clist.begin(at);
			List::const_iterator cend = clist.end(at);
			BOOST_CHECK(cit == cit);
			BOOST_CHECK(cit == cit2);
			BOOST_CHECK(!(cit == cend));
			BOOST_CHECK(!(cit != cit));
			BOOST_CHECK(!(cit != cit2));
			BOOST_CHECK(cit != cend);
						
			List::reverse_iterator rit = list.rbegin(at);
			List::reverse_iterator rit2 = list.rbegin(at);
			List::reverse_iterator rend = list.rend(at);
			BOOST_CHECK(rit == rit);
			BOOST_CHECK(rit == rit2);
			BOOST_CHECK(!(rit == rend));
			BOOST_CHECK(!(rit != rit));
			BOOST_CHECK(!(rit != rit2));
			BOOST_CHECK(rit != rend);

			List::const_reverse_iterator crit = clist.rbegin(at);
			List::const_reverse_iterator crit2 = clist.rbegin(at);
			List::const_reverse_iterator crend = clist.rend(at);
			BOOST_CHECK(crit == crit);
			BOOST_CHECK(crit == crit2);
			BOOST_CHECK(!(crit == crend));
			BOOST_CHECK(!(crit != crit));
			BOOST_CHECK(!(crit != crit2));
			BOOST_CHECK(crit != crend);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterPtr)
{
	struct Test
	{
		typedef void result_type;

		struct TestVal
		{
			TestVal(int val_): val(val_) {}
			int val;
		};

		void operator()(WAtomic& at) const
		{
			typedef WSTMList<TestVal> List;
			List list;
			const int VAL1 = 355240;
			list.push_back(TestVal(VAL1), at);
			const int VAL2 = 28975;
			list.push_back(TestVal(VAL2), at);

			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, it->val);
			List::const_iterator cit = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, cit->val);
			List::reverse_iterator rit = list.rbegin(at);
			BOOST_CHECK_EQUAL(VAL2, rit->val);
			List::const_reverse_iterator crit = list.rbegin(at);
			BOOST_CHECK_EQUAL(VAL2, crit->val);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterDeref)
{
	struct Test
	{
		typedef void result_type;
		void operator()(WAtomic& at) const
		{
			typedef WSTMList<int> List;
			List list;
			const int VAL1 = 355240;
			list.push_back(VAL1, at);
			const int VAL2 = 28975;
			list.push_back(VAL2, at);

			List::iterator it = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *it);
			List::const_iterator cit = list.begin(at);
			BOOST_CHECK_EQUAL(VAL1, *cit);
			List::reverse_iterator rit = list.rbegin(at);
			BOOST_CHECK_EQUAL(VAL2, *rit);
			List::const_reverse_iterator crit = list.rbegin(at);
			BOOST_CHECK_EQUAL(VAL2, *crit);
		}
	};
	Atomically(boost::bind(Test(), _1));
}

BOOST_AUTO_TEST_CASE (test_iterTypedefs)
{
	typedef WSTMList<int> List;
	BOOST_CHECK((is_same<std::bidirectional_iterator_tag,
					List::iterator::iterator_category>::value));
	BOOST_CHECK((is_same<int, List::iterator::value_type>::value));
	BOOST_CHECK((is_same<int, List::iterator::difference_type>::value));
	BOOST_CHECK((is_same<int*, List::iterator::pointer>::value));
	BOOST_CHECK((is_same<int&, List::iterator::reference>::value));
				
	BOOST_CHECK((is_same<std::bidirectional_iterator_tag,
					List::const_iterator::iterator_category>::value));
	BOOST_CHECK((is_same<int, List::const_iterator::value_type>::value));
	BOOST_CHECK((is_same<int, List::const_iterator::difference_type>::value));
	BOOST_CHECK((is_same<const int*, List::const_iterator::pointer>::value));
	BOOST_CHECK((is_same<const int&, List::const_iterator::reference>::value));

	BOOST_CHECK((is_same<std::bidirectional_iterator_tag,
					List::reverse_iterator::iterator_category>::value));
	BOOST_CHECK((is_same<int, List::reverse_iterator::value_type>::value));
	BOOST_CHECK((is_same<int, List::reverse_iterator::difference_type>::value));
	BOOST_CHECK((is_same<int*, List::reverse_iterator::pointer>::value));
	BOOST_CHECK((is_same<int&, List::reverse_iterator::reference>::value));
				
	BOOST_CHECK((is_same<std::bidirectional_iterator_tag,
					List::const_reverse_iterator::iterator_category>::value));
	BOOST_CHECK((is_same<int, List::const_reverse_iterator::value_type>::value));
	BOOST_CHECK((is_same<int, List::const_reverse_iterator::difference_type>::value));
	BOOST_CHECK((is_same<const int*, List::const_reverse_iterator::pointer>::value));
	BOOST_CHECK((is_same<const int&, List::const_reverse_iterator::reference>::value));
}

BOOST_AUTO_TEST_CASE (test_iterBase)
{
	typedef WSTMList<int> List;
	static const int VAL1 = 72487;
	static const int VAL2 = 747785;
	static const int VAL3 = 12739;
	struct Test
	{
		typedef WSTMList<int> List;
		void run(List& list, WAtomic& at)
		{
			List::reverse_iterator rit = list.rbegin(at);
			List::iterator itEnd = rit.base();
			BOOST_CHECK(itEnd == list.end(at));
			++rit;
			List::iterator it = rit.base();
			BOOST_CHECK_EQUAL(VAL3, *it);
			++rit;
			List::iterator it2 = rit.base();
			BOOST_CHECK_EQUAL(VAL2, *it2);

			List::const_reverse_iterator crit = list.rbegin(at);
			List::const_iterator citEnd = crit.base();
			BOOST_CHECK(citEnd == list.end(at));
			++crit;
			List::const_iterator cit = crit.base();
			BOOST_CHECK_EQUAL(VAL3, *cit);
			++crit;
			List::const_iterator cit2 = crit.base();
			BOOST_CHECK_EQUAL(VAL2, *cit2);
		}
					
	};

	List list;
	list.push_back(VAL1);
	list.push_back(VAL2);
	list.push_back(VAL3);
   Test t;
	Atomically(boost::bind(&Test::run, boost::ref (t), boost::ref(list), _1));
}

BOOST_AUTO_TEST_CASE (test_rangeCopyCtor)
{
	static const int VAL1 = 857288;
	static const int VAL2 = 603314;
	struct Test
	{
		void run(const std::vector<int>& original, WAtomic& at)
		{
			typedef WSTMList<int> List;
			List list(original.begin(), original.end(), at);
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			BOOST_CHECK(std::equal(list.begin(at), list.end(at), original.begin()));
		}
	};
	std::vector<int> original;
	original.push_back(VAL1);
	original.push_back(VAL2);

   Test t;
	Atomically(boost::bind(&Test::run, boost::ref (t), boost::cref(original), _1));				

	typedef WSTMList<int> List;
	List list(original.begin(), original.end());
	struct Check
	{
		typedef WSTMList<int> List;
		void run(const std::vector<int>& original, const List& list, WAtomic& at)
		{
			BOOST_CHECK_EQUAL(static_cast<size_t>(2), list.size(at));
			BOOST_CHECK(std::equal(list.begin(at), list.end(at), original.begin()));
		}
	};
   Check c;
   Atomically(
      boost::bind(&Check::run, boost::ref (c), boost::cref(original), boost::cref(list), _1));
}

BOOST_AUTO_TEST_SUITE_END (/*STMListTests*/)
