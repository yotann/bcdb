#ifndef MEMODB_SUPPORT_H
#define MEMODB_SUPPORT_H

#include <type_traits>

namespace memodb {

// Allows static_assert to be used to mark a template instance as
// unimplemented.
template <class T> struct Unimplemented : std::false_type {};

// Helps create a callable type for use with std::visit.
// https://en.cppreference.com/w/cpp/utility/variant/visit
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // end namespace memodb

#endif // MEMODB_SUPPORT_H
