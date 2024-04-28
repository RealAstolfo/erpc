#include <iostream>
#include <variant>
#include <vector>

// By defaulting to void as the derived type, we can refer to "base" as a type
// without a template specialization, this makes it more intuitive than always
// having to explicitly describe it
template <typename derived_type = void> struct base;

template <> struct base<void> {
  void my_func() { std::cout << "Hello from base" << std::endl; }
};

template <> struct base<struct foo_t> {
  void my_func() {
    reinterpret_cast<base<> *>(this)->my_func();
    std::cout << "Hello from derived" << std::endl;
  }
};
using foo = base<foo_t>;

template <> struct base<struct bar_t> {
  void my_func() {
    std::cout << "buzz..." << std::endl;

    // Technically this should not be allowed to happen, it shouldnt with
    // traditional inheritence. But its kinda neat.
    reinterpret_cast<foo *>(this)->my_func();
  }
};
using bar = base<bar_t>;

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  using unionizer = std::variant<base<>, foo, bar>;
  std::vector<unionizer> collection;

  {
    base<> my_base;
    foo my_foo;
    bar my_bar;
    collection.emplace_back(std::move(my_base));
    collection.emplace_back(std::move(my_foo));
    collection.emplace_back(std::move(my_bar));
  }

  for (auto &obj : collection)
    std::visit([](auto &obj) { obj.my_func(); }, obj);

  return 0;
}
