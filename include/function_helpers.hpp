#ifndef ERPC_FUNCTION_HELPERS
#define ERPC_FUNCTION_HELPERS

#include <tuple>

template <typename Fun>
concept is_fun = std::is_function_v<Fun>;

template <typename Fun>
concept is_mem_fun = std::is_member_function_pointer_v<std::decay_t<Fun>>;

template <typename Fun>
concept is_functor = std::is_class_v<std::decay_t<Fun>> &&
                     requires(Fun &&t) { &std::decay_t<Fun>::operator(); };

template <typename Sig> struct signature;
template <typename Ret, typename... Args> struct signature<Ret(Args...)> {
  using args = std::tuple<Args...>;
  using type =
      std::conditional_t<std::is_same_v<Ret, void>, std::tuple<Args...>,
                         std::tuple<Ret, Args...>>;
  using ret = Ret;
};

template <typename Ret, typename Obj, typename... Args>
struct signature<Ret (Obj::*)(Args...) const> {
  using args = std::tuple<Args...>;
  using type =
      std::conditional_t<std::is_same_v<Ret, void>, std::tuple<Obj, Args...>,
                         std::tuple<Ret, Obj, Args...>>;
  using ret = Ret;
};

template <is_functor T>
auto arguments_t(T &&t)
    -> signature<decltype(&std::decay_t<T>::operator())>::args;

template <is_functor T>
auto arguments_t(const T &t)
    -> signature<decltype(&std::decay_t<T>::operator())>::args;

// template<is_fun T>
// auto arguments_t(T&& t)->signature<T>::type;

template <is_fun T> auto arguments_t(const T &t) -> signature<T>::args;

template <is_mem_fun T>
auto arguments_t(T &&t) -> signature<std::decay_t<T>>::args;

template <is_mem_fun T>
auto arguments_t(const T &t) -> signature<std::decay_t<T>>::args;

template <is_functor T>
auto signature_t(T &&t)
    -> signature<decltype(&std::decay_t<T>::operator())>::type;

template <is_functor T>
auto signature_t(const T &t)
    -> signature<decltype(&std::decay_t<T>::operator())>::type;

// template<is_fun T>
// auto signature_t(T&& t)->signature<T>::type;

template <is_fun T> auto signature_t(const T &t) -> signature<T>::type;

template <is_mem_fun T>
auto signature_t(T &&t) -> signature<std::decay_t<T>>::type;

template <is_mem_fun T>
auto signature_t(const T &t) -> signature<std::decay_t<T>>::type;

template <is_functor T>
auto return_t(T &&t) -> signature<decltype(&std::decay_t<T>::operator())>::ret;

template <is_functor T>
auto return_t(const T &t)
    -> signature<decltype(&std::decay_t<T>::operator())>::ret;

// template<is_fun T>
// auto return_t(T&& t)->signature<T>::type;

template <is_fun T> auto return_t(const T &t) -> signature<T>::ret;

template <is_mem_fun T> auto return_t(T &&t) -> signature<std::decay_t<T>>::ret;

template <is_mem_fun T>
auto return_t(const T &t) -> signature<std::decay_t<T>>::ret;

#endif
