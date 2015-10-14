#include <type_traits>


namespace WSTM
{
   //!{
   /**
    * Finds the given type in the given pack of arguments. This is useful for having variadic
    * argument lists for functions. To have variadic arguments in a function just make the function
    * a template and take a parameter pack of arguments. Then use findArg to find any given type in
    * the parameter pack (each argument you want to be able to find needs to have a unique type,
    * which is easy to arrange by using the FIND_ARG__MAKE_ARG_TYPE macro). findArg will return the
    * argument of the given type or a default constructed object of the given type if there was no
    * object of the given type in the argument pack.
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
   typename Wanted_t findArg ()
   {
      return Wanted_t ();
   }
   //!}
   
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
   
}
