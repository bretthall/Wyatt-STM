/****************************************************************************
 Wyatt Technology Corporation
 6300 Hollister Avenue
 Goleta, CA 93117

 Copyright (c) 2002-2015. All rights reserved.
****************************************************************************/


#define BOOST_TEST_MODULE wstm-tests
#include <boost/test/unit_test.hpp>

#ifdef NON_APPLE_CLANG 
//Clang on linux is missing this
extern "C" int __cxa_thread_atexit(void (*func)(), void *obj, void *dso_symbol)
{
   int __cxa_thread_atexit_impl(void (*)(), void *, void *);
   return __cxa_thread_atexit_impl(func, obj, dso_symbol);
}
#endif //NON_APPLE_CLANG 
