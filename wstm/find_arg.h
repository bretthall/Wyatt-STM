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

#include <type_traits>


/**
 * @file find_arg.h
 * A system for finding specific types in a parameter pack.
 */

namespace WSTM
{
   /**
    * @defgroup FindArg Argument Finder
    *
    * Finds arguments of a given type in a parameter pack.
    */
   ///@{

   //@{
   /**
    * Finds the given type in the given pack of arguments. This is useful for having variadic
    * argument lists for functions. To have variadic arguments in a function just make the function
    * a template and take a parameter pack of arguments. Then use findArg to find any given type in
    * the parameter pack (each argument you want to be able to find needs to have a unique type,
    * which is easy to arrange by using the FIND_ARG__MAKE_ARG_TYPE macro). findArg will return the
    * argument of the given type or a default constructed object of the given type if there was no
    * object of the given type in the argument pack.
    *
    * @param Wanted_t The type to find. 
    * @param as The arguments to search through.
    */
   template <typename Wanted_t, typename A_t, typename ... As_t>
   std::enable_if_t<std::is_same<Wanted_t, A_t>::value, Wanted_t> findArg (const A_t& a, const As_t&...)
   {
      return a;
   }

   template <typename Wanted_t, typename A_t, typename ... As_t>
   std::enable_if_t<!std::is_same<Wanted_t, A_t>::value, Wanted_t> findArg (const A_t&, const As_t&... as)
   {
      return findArg<Wanted_t> (as...);
   }

   template <typename Wanted_t, typename A_t>
   std::enable_if_t<std::is_same<Wanted_t, A_t>::value, Wanted_t> findArg (const A_t& a)
   {
      return a;
   }

   template <typename Wanted_t, typename A_t>
   std::enable_if_t<!std::is_same<Wanted_t, A_t>::value, Wanted_t> findArg (const A_t&)
   {
      return Wanted_t ();
   }

   template <typename Wanted_t>
   Wanted_t findArg ()
   {
      return Wanted_t ();
   }
   //@}
   
   /**
    * Creates a type for use with findArg. The created type will have a "m_value" member that
    * contains the actual value for the argument.
    *
    * @param type The type for m_value.
    * @param name The name that shoudl be given to the create type.
    * @param def The default value for m_value.
    */
#define FIND_ARG__MAKE_ARG_TYPE(type, name, def)             \
   struct name                                               \
   {                                                         \
      type m_value;                                          \
      name () : m_value (def) {}                             \
      name (const type value) : m_value (value) {}           \
   };//

   ///@}
}
