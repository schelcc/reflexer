#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
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

template <typename K, typename V>
  requires(
      requires(K k) { std::hash<K>{}(k); } &&
      requires(K k1, K k2) { k1 == k2; })
struct Map {
  /** @brief Inserts the value into the table with the given key. Requires that
   * the key is not already present. */
  constexpr void insert(K &&k, V &&v) {
    maybe_rehash();
    std::size_t idx = find_open(k);
    _keys[idx].emplace(std::move(k));
    _vals[idx].emplace(std::move(v));
  }

  /** @brief Updates the key with the given value. Requires that the key is
   * present in the table, throwing if not. */
  constexpr void update(K const &k, V &&v) {
    std::optional<std::size_t> idx = find(k);
    if (!idx.has_value())
      throw std::invalid_argument("Key not in table");
    _vals[idx].emplace(std::move(v));
  }

  /** @brief Either updates or inserts the value in the table according to
   * whether the key was already present or not. Returns boolean indicating
   * whether the key was inserted. */
  template <typename U> constexpr bool insert_or_update(U &&u, V &&v) {
    auto pos = find(u);
    if (!pos.has_value()) {
      std::size_t idx = pos.error();
      if (maybe_rehash())
        idx = find_open(u);

      _keys[idx].emplace(std::forward<U>(u));
      _vals[idx].emplace(std::move(v));
      return true;
    } else {
      std::size_t idx = pos.value();
      _vals[idx].emplace(std::move(v));
      return false;
    }
    std::unreachable();
  }

  /** @brief Insert the value into the table only if the key is not already
   * present. Does not touch the value if the key is present. Returns boolean
   * indicating whether the value was added to the table. */
  template <typename U, typename W> constexpr bool try_insert(U &&u, W &&w) {
    auto pos = find(u);
    if (!u.has_value()) {
      std::size_t idx = u.error();

      if (maybe_rehash())
        idx = find_open(u);

      _keys[idx].emplace(std::forward<U>(u));
      _vals[idx].emplace(std::forward<W>(w));
      return true;
    }
    return false;
  }

  /** @brief Remove the entry for the given key, doing nothing if the key is not
   * present. Returns a boolean indicating whether the key was removed from the
   * table. */
  constexpr bool remove(K const &k) noexcept {
    auto pos = find(k);
    if (pos.has_value()) {
      std::size_t idx = pos.value();
      _keys[idx].reset();
      _vals[idx].reset();
      _elements--;
      _load_factor =
          static_cast<double>(_elements) / static_cast<double>(_keys.size());
      return true;
    }
    return false;
  }

  /** @brief Remove all entries from the table. */
  constexpr void clear() noexcept {
    _elements = 0;
    _load_factor = 0;
    _keys = std::vector<std::optional<K>>(_keys.size(), std::nullopt);
    _vals = std::vector<std::optional<V>>(_vals.size(), std::nullopt);
  }

  /** @brief Return a reference to the value at the key, if the key is present
   * in the table. */
  constexpr std::optional<V &> at(K const &k) noexcept {
    auto pos = find(k);
    if (pos.has_value())
      return _vals[pos.value()].value();
    else
      return std::nullopt;
  }

  /** @brief Retrieve a reference to the value at the key, throwing if the key
   * is not present in the table. */
  constexpr V &at_throwing(K const &k) {
    auto pos = find(k);
    if (pos.has_value())
      return _vals[pos.value()].value();
    else
      throw std::invalid_argument("Key is not present in the table");
  }

  /** @brief Return whether the given key is present in the table. */
  constexpr bool contains(K const &k) const noexcept {
    return find(k).has_value();
  }

  /** @brief Return the number of elements present in the table. */
  constexpr std::size_t size() const noexcept { return _elements; }

  /** @brief Return the table's current load factor. */
  constexpr double load_factor() const noexcept { return _load_factor; }

  /** @brief Return whether the table is empty. */
  constexpr bool empty() const noexcept { return _elements == 0; }

private:
  /** @brief Update the number of elements and load factor, rehashing the table
   * if the load factor exceeds the configured threshold. Returns a boolean
   * indicating whether the table was rehashed. */
  constexpr bool maybe_rehash() {
    _elements++;
    _load_factor =
        static_cast<double>(_elements) / static_cast<double>(_keys.size());
    if (_load_factor >= LOAD_FACTOR_THRESH) {
      rehash();
      return true;
    } else
      return false;
  }

  /** @brief Calculates the hash for the given key. */
  constexpr std::size_t hash(K const &k) const {
    return std::hash<K>{}(k) % _keys.size();
  }

  /** @brief Finds the first open slot for the given key. */
  constexpr std::size_t find_open(K const &k) const {
    std::size_t h = hash(k);
    std::size_t probe = 0;
    while (_keys[(h + probe) % _keys.size()].has_value())
      probe += PROBE_STEP;
    return (h + probe) % _keys.size();
  }

  /** @brief Resize and rehash the table. Invalidates all previous indices. */
  constexpr void rehash() {
    auto tmp_keys = std::move(_keys);
    auto tmp_vals = std::move(_vals);

    _keys = std::vector<std::optional<K>>(tmp_keys.size() * 2, std::nullopt);
    _vals = std::vector<std::optional<V>>(tmp_keys.size() * 2, std::nullopt);

    _load_factor /= 2.0;

    for (auto &&[k, v] :
         std::ranges::zip_view{tmp_keys, tmp_vals} | std::views::as_rvalue) {
      if (k.has_value()) {
        std::size_t idx = find_open(k.value());
        _keys[idx].emplace(std::move(k.value()));
        _vals[idx].emplace(std::move(v.value()));
      }
    }
  }

  /** @brief Find the given key, or the first open slot at which it could be
   * placed. Returns an expected which contains an index in both states. If the
   * expected is in the error state, the index represents the place at which the
   * key could be placed, otherwise it represents the index of that element in
   * the table. */
  constexpr std::expected<std::size_t, std::size_t> find(K const &k) const {
    std::size_t h = hash(k);
    std::size_t probe = 0;
    std::size_t idx = (h + probe) % _keys.size();
    while (_keys[idx].has_value()) {
      if (_keys[idx].value() == k)
        return idx;
      else {
        probe += PROBE_STEP;
        idx = (h + probe) % _keys.size();
      }
    }
    return std::unexpected(idx);
  }

  /// @brief The step size used for linear probing
  static constexpr std::size_t PROBE_STEP = 1;

  /// @brief The load factor at or after which to rehash
  static constexpr double LOAD_FACTOR_THRESH = 0.75;

  std::size_t _elements = 0;
  double _load_factor = 0;

  std::vector<std::optional<K>> _keys =
      std::vector<std::optional<K>>(4, std::nullopt);

  std::vector<std::optional<V>> _vals =
      std::vector<std::optional<V>>(4, std::nullopt);
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
