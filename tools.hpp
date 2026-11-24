#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <iterator>
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
concept Hashable = requires(T t) { std::hash<T>{}(t); };

template <typename T>
concept IsEqComparable = requires(T t1, T t2) { t1 == t2; };

/** @brief Iterator type which wraps the iterator of a vector of
 * std::optional<T>, skipping over any empty indices upon increment/decrement.
 */
template <typename T> struct SkippingIterator {

  // Required to hold onto start/end iterators so we can safely deref the vec
  // iterator to check optional engagement
  constexpr SkippingIterator(
      std::vector<std::optional<T>>::iterator &&it,
      std::vector<std::optional<T>>::iterator const &start_it,
      std::vector<std::optional<T>>::iterator const &end_it)
      : _it(std::move(it)), _start_it(start_it), _end_it(end_it) {}

  constexpr SkippingIterator &operator+=(int const amt) {
    const int step = (amt < 0) ? -1 : 1;
    int steps = 0;
    while (steps < amt) {
      do {
        _it += step;
      } while ((_it < _end_it) && !_it->has_value());
      steps++;
    }
    return *this;
  }
  constexpr SkippingIterator &operator-=(int const amt) {
    return operator+=(-amt);
  }

  constexpr SkippingIterator &operator--() { return *this -= 1; }
  constexpr SkippingIterator operator--(int) {
    auto ret = *this;
    *this -= 1;
    return ret;
  }

  /* required by weakly_incrementable */
  using difference_type = std::ptrdiff_t;
  constexpr SkippingIterator &operator++() { return *this += 1; }
  constexpr SkippingIterator operator++(int) {
    auto ret = *this;
    *this += 1;
    return ret;
  }

  /* required by indirectly_readable */
  using value_type = T;
  constexpr auto &&operator*(this auto &&self) {
    return std::forward_like<decltype(self)>(self._it)->value();
  }

  /* required by WeakEqualityComparable (expositional concept, required I is
   * equality comparable with itself) */
  [[nodiscard]] bool operator==(SkippingIterator const &o) const {
    return _it == o._it;
  }
  [[nodiscard]] bool operator!=(SkippingIterator const &o) const {
    return _it != o._it;
  }
  [[nodiscard]] std::weak_ordering
  operator<=>(SkippingIterator const &o) const {
    return _it <=> o._it;
  }

  constexpr SkippingIterator operator+(int const amt) {
    auto ret = *this;
    ret._it += amt;
    return ret;
  }
  constexpr SkippingIterator operator-(int const amt) { return *this + -amt; }

  /* required by semiregular */
  constexpr SkippingIterator() = default;
  constexpr SkippingIterator(SkippingIterator const &) = default;
  constexpr SkippingIterator &operator=(SkippingIterator const i) {
    _it = i._it;
    _end_it = i._end_it;
    _start_it = i._start_it;
  };
  constexpr SkippingIterator(SkippingIterator &&) = default;
  constexpr SkippingIterator &operator=(SkippingIterator &&) = default;

private:
  [[nodiscard]] constexpr bool inrange_below() const noexcept {
    return (_it >= _start_it);
  }
  [[nodiscard]] constexpr bool inrange_above() const noexcept {
    return (_it < _end_it);
  }

  const std::vector<std::optional<T>>::iterator _start_it;
  const std::vector<std::optional<T>>::iterator _end_it;
  std::vector<std::optional<T>>::iterator _it;
};

template <typename K>
  requires(Hashable<K> && IsEqComparable<K>)
struct Set {
  /** @brief Insert the key into the set if not already present. Returns boolean
   * indicating whether the key was added to the set. */
  template <typename U> constexpr bool insert(U &&u) {
    auto pos = find(u);
    if (!pos.has_value()) {
      std::size_t idx = pos.error();
      if (maybe_rehash())
        idx = find_open(u);
      _keys[idx].emplace(std::forward<U>(u));
      return true;
    }
    return false;
  }

  /** @brief Remove the key from the set if it is present. Returns boolean
   * indicating whether the key was present and is now removed. */
  constexpr bool remove(K &&k) {
    auto pos = find(k);
    if (pos.has_value()) {
      std::size_t idx = pos.value();
      _keys[idx].reset();
      _elements--;
      _load_factor =
          static_cast<double>(_elements) / static_cast<double>(_keys.size());
      return true;
    }
    return false;
  }

  /** @brief Remove all entries from the set. */
  constexpr bool clear() {
    _elements = 0;
    _load_factor = 0;
    _keys = std::vector<std::optional<K>>(_keys.size(), std::nullopt);
  }

  /** @brief Determine if the set is currently empty. */
  constexpr bool empty() const noexcept { return _elements == 0; }

  /** @brief Determine if the set contains the given key. */
  template <typename U> constexpr bool contains(K const &k) const noexcept {
    return find(k).has_value();
  }

  /** @brief Determine the number of elements present in the set. */
  constexpr std::size_t size() const noexcept { return _elements; }

  /** @brief Determine the number of total slots in the current set. */
  constexpr std::size_t max_size() const noexcept { return _keys.size(); }

  /** @brief Determine the current load factor of the set. */
  constexpr double load_factor() const noexcept { return _load_factor; }

  constexpr SkippingIterator<K> begin() {
    auto it = _keys.begin();
    while (!it->has_value())
      it++;
    return SkippingIterator<K>(std::move(it), _keys.begin(), _keys.end());
  }

  constexpr SkippingIterator<K> end() {
    return SkippingIterator<K>(_keys.end(), _keys.begin(), _keys.end());
  }

private:
  /** @brief Update the number of elements and load factor, rehashing the set if
   * the load factor exceeds the configured threshold. Returns a boolean
   * indicating whether the set was rehashed. */
  constexpr bool maybe_rehash() {
    _elements++;
    _load_factor =
        static_cast<double>(_elements) / static_cast<double>(_keys.size());
    if (_load_factor >= LOAD_FACTOR_THRESH) {
      rehash();
      return true;
    }
    return false;
  }

  /** @brief Calculate the hash for the given key. */
  constexpr std::size_t hash(K const &k) const {
    return std::hash<K>{}(k) % _keys.size();
  }

  /** @brief Find the first open slot for the given key. */
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

    _keys = std::vector<std::optional<K>>(tmp_keys.size() * 2, std::nullopt);
    _load_factor /= 2.0;

    for (auto &&k : std::move(tmp_keys))
      if (k.has_value())
        _keys[find_open(k.value())].emplace(std::move(k.value()));
  }

  /** @brief Find the given key, or the first open slot at which it could be
   * placed. Returns an expected which contains an index in both states. If the
   * expected is in the error state, the index represents the place at which the
   * key could be placed, otherwise it represents the index of that element in
   * the set. */
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
};

template <typename K, typename V>
  requires(Hashable<K> && IsEqComparable<K>)
struct Map {
  /** @brief Inserts the value into the table with the given key. Requires that
   * the key is not already present. */
  constexpr void insert(K &&k, V &&v) {
    maybe_rehash();
    std::size_t idx = find_open(k);
    _table[idx].emplace(std::move(k), std::move(v));
  }

  /** @brief Updates the key with the given value. Requires that the key is
   * present in the table, throwing if not. */
  constexpr void update(K const &k, V &&v) {
    std::optional<std::size_t> idx = find(k);
    if (!idx.has_value())
      throw std::invalid_argument("Key not in table");
    _table[idx].value().second = std::move(v);
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

      _table[idx].emplace(std::forward<U>(u), std::move(v));
      return true;
    } else {
      std::size_t idx = pos.value();
      _table[idx].value().second = std::move(v);
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

      _table[idx].emplace(std::forward<U>(u), std::forward<W>(w));
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
      _table[idx].reset();
      _elements--;
      _load_factor =
          static_cast<double>(_elements) / static_cast<double>(_table.size());
      return true;
    }
    return false;
  }

  /** @brief Remove all entries from the table. */
  constexpr void clear() noexcept {
    _elements = 0;
    _load_factor = 0;
    _table = std::vector<std::optional<K>>(_table.size(), std::nullopt);
  }

  /** @brief Return a reference to the value at the key, if the key is present
   * in the table. */
  constexpr std::optional<V &> at(K const &k) noexcept {
    auto pos = find(k);
    if (pos.has_value())
      return _table[pos.value()].value().second;
    else
      return std::nullopt;
  }

  /** @brief Retrieve a reference to the value at the key, throwing if the key
   * is not present in the table. */
  constexpr V &at_throwing(K const &k) {
    auto pos = find(k);
    if (pos.has_value())
      return _table[pos.value()].value().second;
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

  constexpr SkippingIterator<Pair<K, V>> begin() {
    return {_table.begin(), _table.begin(), _table.end()};
  }

  constexpr SkippingIterator<Pair<K, V>> end() {
    return {_table.end(), _table.begin(), _table.end()};
  }

private:
  /** @brief Update the number of elements and load factor, rehashing the table
   * if the load factor exceeds the configured threshold. Returns a boolean
   * indicating whether the table was rehashed. */
  constexpr bool maybe_rehash() {
    _elements++;
    _load_factor =
        static_cast<double>(_elements) / static_cast<double>(_table.size());
    if (_load_factor >= LOAD_FACTOR_THRESH) {
      rehash();
      return true;
    } else
      return false;
  }

  /** @brief Calculates the hash for the given key. */
  constexpr std::size_t hash(K const &k) const {
    return std::hash<K>{}(k) % _table.size();
  }

  /** @brief Finds the first open slot for the given key. */
  constexpr std::size_t find_open(K const &k) const {
    std::size_t h = hash(k);
    std::size_t probe = 0;
    while (_table[(h + probe) % _table.size()].has_value())
      probe += PROBE_STEP;
    return (h + probe) % _table.size();
  }

  /** @brief Resize and rehash the table. Invalidates all previous indices. */
  constexpr void rehash() {
    auto tmp_table = std::move(_table);

    _table = std::vector<std::optional<Pair<K, V>>>(tmp_table.size() * 2,
                                                    std::nullopt);

    _load_factor /= 2.0;

    for (auto &&e : std::move(tmp_table)) {
      if (e.has_value()) {
        std::size_t idx = find_open(e.value().first);
        _table[idx] = std::move(e);
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
    std::size_t idx = (h + probe) % _table.size();
    while (_table[idx].has_value()) {
      if (_table[idx].value().first == k)
        return idx;
      else {
        probe += PROBE_STEP;
        idx = (h + probe) % _table.size();
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

  std::vector<std::optional<Pair<K, V>>> _table =
      std::vector<std::optional<Pair<K, V>>>(4, std::nullopt);
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
