#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace Reflexer::Methods {

template <typename T1, typename T2> struct Pair {
  T1 first;
  T2 second;
};

template <typename T> struct Deque {
  template <typename... Ts>
    requires std::is_constructible_v<T, Ts...>
  constexpr void enqueue_back(Ts &&...ts) {
    _vec.emplace_back(std::forward<Ts>(ts)...);
  }

  template <typename... Ts>
    requires std::is_constructible_v<T, Ts...>
  constexpr void enqueue_front(Ts &&...ts) {
    _vec.emplace(_vec.begin(), std::forward<Ts>(ts)...);
  }

  constexpr T dequeue_back() {
    auto tmp = _vec.back();
    _vec.pop_back();
    return tmp;
  }

  constexpr T dequeue_front() {
    auto tmp = _vec.front();
    _vec.erase(_vec.begin());
    return tmp;
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return _vec.empty(); }
  [[nodiscard]] constexpr size_t size() const noexcept { return _vec.size(); }
  [[nodiscard]] constexpr T back() noexcept { return _vec.back(); }
  [[nodiscard]] constexpr T front() noexcept { return _vec.front(); }

private:
  std::vector<T> _vec{};
};

template <typename T>
constexpr std::optional<size_t> index_of(std::vector<T> const &v, T const &t) {
  size_t idx = 0;
  for (auto const &e : v)
    if (e == t)
      return idx;
    else
      idx++;

  return std::nullopt;
}

template <typename T, typename F>
  requires(std::is_invocable_r_v<bool, F, T, T>)
constexpr std::optional<size_t> index_of(std::vector<T> const &v, T const &t,
                                         F &&cmp) {
  size_t idx = 0;
  for (auto const &e : v)
    if (cmp(e, t))
      return idx;
    else
      idx++;

  return std::nullopt;
}

/*
template <typename K, typename V>
  requires requires(K k1, K k2) { k1 <=> k2; }
class AssocVec {
  struct Entry {
    K k;
    V v;
  };

  std::vector<Entry> _vec{};
  std::size_t _midpt = 0;
  std::optional<K> _midpt_val = std::nullopt;

  constexpr size_t find_insertion_pt(K const &k) const {
    if (_vec.empty())
      return 0;

    std::size_t cand = _midpt;
    std::size_t left = 0;
    std::size_t right = _vec.size();

  }

public:
  template <typename K_In, typename V_In>
  requires (std::is_same_v<std::remove_cvref_t<K_In>, K>
    && std::is_same_v<std::remove_cvref_t<V_In>, V>)
  void insert(K_In &&k, V_In &&v) {

  }
};
*/

}; // namespace Reflexer::Methods
