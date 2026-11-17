#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <deque>
#include <expected>
#include <meta>
#include <optional>
#include <print>
#include <queue>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#define TRY_DROP(expr)                                                         \
  {                                                                            \
    auto _ = (expr);                                                           \
    if (!_.has_value())                                                        \
      return _.error();                                                        \
  }

#define TRY_USE(name, expr)                                                    \
  auto RESULT_##name = (expr);                                                 \
  if (!RESULT_##name.has_value())                                              \
    return RESULT_##name.error();                                              \
  auto name = std::move(RESULT_##name).value();

template <typename T> struct Deque {
  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  void emplace_top(Args &&...args) {
    _data.emplace_back(std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  void emplace_bottom(Args &&...args) {
    _data.emplace_front(std::forward<Args>(args)...);
  }

  [[nodiscard]] auto &&top(this auto &&self) {
    assert(self._data.size() > 0);
    return std::forward_like<decltype(self)>(self._data.back());
  }

  [[nodiscard]] auto &&bottom(this auto &&self) {
    assert(self._data.size() > 0);
    return std::forward_like<decltype(self)>(self._data.front());
  }

  [[nodiscard]] T pop_top() {
    assert(_data.size() > 0);
    auto tmp = std::move(_data.back());
    _data.pop_back();
    return tmp;
  }

  [[nodiscard]] T pop_bottom() {
    assert(_data.size() > 0);
    auto tmp = std::move(_data.front());
    _data.pop_front();
    return tmp;
  }

  [[nodiscard]] size_t size() const noexcept { return _data.size(); }

  [[nodiscard]] bool empty() const noexcept { return _data.empty(); }

  void pop_then_push_to(Deque &push_to) { push_to.emplace_top(pop_top()); }

private:
  std::deque<T> _data;
};

template <typename Var> struct VarType {
  size_t idx = 0;

  consteval VarType(std::meta::info r) {
    size_t i = 0;
    while (i < std::meta::variant_size(^^Var)) {
      if (std::meta::is_same_type(std::meta::variant_alternative(i, ^^Var),
                                  r)) {
        idx = i;
        return;
      }
      ++i;
    }
    throw std::invalid_argument(
        "Type " + std::string(std::meta::display_string_of(r)) +
        " is not a valid alternative for the variant " +
        std::string(std::meta::display_string_of(^^Var)));
  }

  consteval operator size_t() { return idx; };
};

struct ParseErr {
  enum Kind : uint8_t {
    UNCLOSED_GROUP = 0,
    UNCLOSED_CLASS,
    CLOSING_WITHOUT_OPEN,
    INCOMPLETE_SUBEXPR,
    EMPTY_EXPR,
    INVALID_ENDMARKER,
  };

  Kind kind;
  std::string msg;

  ParseErr(Kind k) : kind(k), msg("") {}
  ParseErr(Kind k, std::string &&s) : kind(k), msg(std::move(s)) {}

  template <typename S> operator std::expected<S, ParseErr>() const noexcept {
    return std::unexpected(*this);
  }
};

struct Char {
  char rule;
  size_t pos;
};

struct Union {
  std::optional<size_t> left_child;
  std::optional<size_t> right_child;

  Union() = default;
  Union(size_t l) : left_child(l), right_child(std::nullopt) {}
  Union(size_t l, size_t r) : left_child(l), right_child(r) {}
};

struct Concat {
  std::optional<size_t> left_child;
  std::optional<size_t> right_child;

  Concat() = default;
  Concat(size_t l) : left_child(l), right_child(std::nullopt) {}
  Concat(size_t l, size_t r) : left_child(l), right_child(r) {}
};

struct Repeat {
  std::optional<size_t> child;

  size_t min_repeat;
  std::optional<size_t> max_repeat;

  Repeat() = default;
  Repeat(size_t c) : child(c), min_repeat(0), max_repeat(std::nullopt) {}
  Repeat(size_t c, size_t r)
      : child(c), min_repeat(r), max_repeat(std::nullopt) {}
  Repeat(size_t c, size_t r, size_t m)
      : child(c), min_repeat(r), max_repeat(m) {}
};

struct Any {
  size_t pos;
};

struct EndMarker {
  size_t pos;
};

using RuleTypes = std::variant<Char, Union, Concat, Repeat, Any, EndMarker>;

struct Rule : RuleTypes {
  using RuleTypes::variant;

  std::optional<bool> nullable = std::nullopt;
  std::optional<std::set<size_t>> firstpos = std::nullopt;
  std::optional<std::set<size_t>> lastpos = std::nullopt;

  template <typename Alt> [[nodiscard]] bool is() {
    return std::holds_alternative<Alt>(*this);
  }

  template <typename Alt> [[nodiscard]] auto &&as(this auto &&self) {
    return std::forward_like<decltype(self)>(std::get<Alt>(self));
  }
};

using PositionSet = std::vector<std::set<size_t>>;
using PositionMap = std::vector<char>;

struct State {
  std::set<size_t> positions{};

  [[nodiscard]] bool operator==(State const &other) const noexcept {
    return positions == other.positions;
  }

  operator std::string() const {
    std::string out = "";
    if (positions.empty())
      return out;

    for (size_t pos : positions)
      out += std::to_string(pos) + ":";

    out.pop_back();
    return out;
  }
};

namespace std {
template <> struct hash<State> {
  inline size_t operator()(State const &s) const {
    return std::hash<std::string>{}(s);
  }
};
}; // namespace std

using TransMap = std::array<int, 256>;

// When building DFA hold around a map of strings of concated state names to vec
// indices, so that we can keep the states in a vec but keep semantic names?
std::pair<std::vector<TransMap>, std::vector<size_t>>
build_dfa(Rule const &root_rule, PositionSet const &followpos,
          PositionMap const &pos_map) {
  std::vector<TransMap> states{};
  std::vector<size_t> accepting_indices{};

  // Keys are ordered sets of positions delimited by :
  std::unordered_map<State, size_t> state_map{};

  Deque<State> to_visit{};
  to_visit.emplace_top(*root_rule.firstpos);
  state_map.emplace(*root_rule.firstpos, 0);

  states.emplace_back();
  states.back().fill(-1);

  while (!to_visit.empty()) {
    State state = std::move(to_visit.pop_bottom());
    size_t state_idx = state_map.at(state);

    std::unordered_map<char, std::set<size_t>> char_to_pos_set{};

    for (size_t pos : state.positions) {
      char c = pos_map.at(pos);
      char_to_pos_set.try_emplace(pos_map.at(pos));
      char_to_pos_set.at(c).emplace(pos);
    }

    if (char_to_pos_set.contains('\0'))
      accepting_indices.emplace_back(state_idx);

    for (auto &&[c, positions] : char_to_pos_set) {
      // Shouldn't have a transition on the null char

      if (c == '\0')
        continue;
      // Iter->second gives the index of the new state, whether added or not
      State new_state{};
      for (size_t pos : positions)
        new_state.positions.insert_range(followpos.at(pos));

      auto [iter, succ] = state_map.try_emplace(new_state, states.size());

      if (succ) {
        states.emplace_back();
        states.back().fill(-1);
        to_visit.emplace_top(std::move(new_state));
      }

      // In the current state, map the current char to the new state
      states.at(state_idx).at(c) = iter->second;
    }
  }

  return {std::move(states), std::move(accepting_indices)};
}

Rule &solve_tree(std::vector<Rule> &rules, PositionSet &followpos,
                 size_t const root_ptr) {
  struct Vis {
    Rule &enclosing_rule;

    std::vector<Rule> &rules;
    PositionSet &followpos;

    void operator()(Union &r) {
      assert(r.left_child.has_value() && r.right_child.has_value());

      Rule &left = solve_tree(rules, followpos, *r.left_child);
      Rule &right = solve_tree(rules, followpos, *r.right_child);

      assert(!left.firstpos->empty());
      assert(!left.lastpos->empty());

      assert(!right.firstpos->empty());
      assert(!right.lastpos->empty());

      enclosing_rule.nullable = *left.nullable || *right.nullable;

      enclosing_rule.firstpos = *left.firstpos;
      enclosing_rule.firstpos->insert_range(*right.firstpos);

      enclosing_rule.lastpos = *left.lastpos;
      enclosing_rule.lastpos->insert_range(*right.lastpos);
    }
    void operator()(Concat &r) {

      assert(r.left_child.has_value() && r.right_child.has_value());

      Rule &left = solve_tree(rules, followpos, *r.left_child);
      Rule &right = solve_tree(rules, followpos, *r.right_child);

      assert(!left.firstpos->empty());
      assert(!left.lastpos->empty());

      assert(!right.firstpos->empty());
      assert(!right.lastpos->empty());

      enclosing_rule.nullable = *left.nullable && *right.nullable;

      enclosing_rule.firstpos = *left.firstpos;
      if (*left.nullable)
        enclosing_rule.firstpos->insert_range(*right.firstpos);

      enclosing_rule.lastpos = *right.lastpos;
      if (*right.nullable)
        enclosing_rule.lastpos->insert_range(*right.lastpos);

      for (size_t i : *left.lastpos)
        followpos.at(i).insert_range(*right.firstpos);
    }
    void operator()(Repeat &r) {
      // For now, only going to handle kleene (min 0, max null)
      assert(r.child.has_value() && !r.max_repeat.has_value());

      Rule &child = solve_tree(rules, followpos, *r.child);

      assert(!child.firstpos->empty());
      assert(!child.lastpos->empty());

      enclosing_rule.nullable = (r.min_repeat == 0);
      enclosing_rule.firstpos = *child.firstpos;
      enclosing_rule.lastpos = *child.lastpos;

      for (size_t i : *child.lastpos)
        followpos.at(i).insert_range(*enclosing_rule.firstpos);
    }
    void operator()(Char &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
    void operator()(Any &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
    void operator()(EndMarker &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
  };

  Rule &rule = rules.at(root_ptr);
  std::visit(Vis{rule, rules, followpos}, rule);
  return rule;
}

std::string_view rule_name(Rule const &rule) {
  return rule.visit(
      [](auto t) { return std::meta::display_string_of(^^decltype(t)); });
}

std::string rule_lisp_str(std::vector<Rule> const &rules, size_t const rule_ptr,
                          std::string prefix = "") {
  auto const &rule = rules.at(rule_ptr);

  return rule.visit([&](auto r) -> std::string {
    constexpr auto rule_refl = ^^decltype(r);
    std::string output =
        std::format("({} ", std::meta::display_string_of(rule_refl));

    std::string data_members = "";
    std::string child_members = "";

    constexpr auto ctx = std::meta::access_context::unchecked();
    constexpr auto ms =
        std::define_static_array(std::meta::members_of(rule_refl, ctx));

    template for (constexpr auto m : auto(ms)) {
      if constexpr (!std::meta::is_function(m)) {
        constexpr auto mt_refl = std::meta::type_of(m);

        if constexpr (std::meta::display_string_of(m).ends_with("child")) {
          if (r.[:m:].has_value())
            child_members.append(std::format(
                "\n  {}({}: {})", prefix, std::meta::identifier_of(m),
                rule_lisp_str(rules, r.[:m:].value(), prefix + "  ")));
          else
            child_members.append(std::format("\n {}({}: ())", prefix,
                                             std::meta::identifier_of(m)));
        } else {
          data_members.append(std::meta::identifier_of(m));
          if constexpr (std::meta::has_template_arguments(mt_refl) &&
                        (std::meta::template_of(mt_refl) == ^^std::optional)) {
            data_members.append(
                r.[:m:].has_value() ? std::format("[{}], ", r.[:m:].value())
                                    : "[], ");
          } else {
            data_members.append(std::format("[{}], ", r.[:m:]));
          }
        }
      }
    }

    if (!data_members.empty())
      output += std::format("[ {} ]",
                            data_members.substr(0, data_members.size() - 2));

    output += child_members;
    return output + ")";
  });
}

std::expected<size_t, ParseErr>
_parse_expr(std::vector<Rule> &rules, PositionMap &pos_map, int &pos_cnt,
            std::string_view::const_iterator &s_it,
            std::string_view::const_iterator const &s_end,
            int group_depth = 0) noexcept {

  Deque<size_t> complete{};

  // Procedure to bottom-to-top concatenate rules, leaving just one rule on the
  // complete stack
  auto summarize_complete = [&] {
    while (complete.size() > 1) {
      size_t left_expr = complete.pop_bottom();
      size_t right_expr = complete.pop_bottom();
      rules.emplace_back(std::in_place_type_t<Concat>(), left_expr, right_expr);
      complete.emplace_bottom(rules.size() - 1);
    }
  };

  while (s_it < s_end) {
    switch (*s_it) {
    case '(': {
      TRY_USE(resulting_rule, _parse_expr(rules, pos_map, pos_cnt, ++s_it,
                                          s_end, group_depth + 1));
      complete.emplace_top(resulting_rule);
      break;
    }
    case ')': {
      if (group_depth == 0)
        return std::unexpected(ParseErr::CLOSING_WITHOUT_OPEN);
      else {
        summarize_complete();
        return complete.top();
      }
      break;
    }
    case '|': {
      if (complete.empty() || (++s_it >= s_end))
        return std::unexpected(ParseErr::INCOMPLETE_SUBEXPR);

      summarize_complete();
      size_t left_expr = complete.pop_top();
      TRY_USE(right_expr,
              _parse_expr(rules, pos_map, pos_cnt, s_it, s_end, group_depth));
      rules.emplace_back(std::in_place_type_t<Union>(), left_expr, right_expr);
      return rules.size() - 1;
    }
    case '*': {
      if (complete.empty())
        return std::unexpected(ParseErr::INCOMPLETE_SUBEXPR);

      // 0 or more, no lower or upper bound
      rules.emplace_back(std::in_place_type_t<Repeat>(), complete.pop_top());
      complete.emplace_top(rules.size() - 1);
      break;
    }
    case '+': {
      if (complete.empty())
        return std::unexpected(ParseErr::INCOMPLETE_SUBEXPR);

      // 1 or more, no upper bound
      rules.emplace_back(std::in_place_type_t<Repeat>(), complete.pop_top(), 1);
      complete.emplace_top(rules.size() - 1);
      break;
    }
    case '?': {
      if (complete.empty())
        return std::unexpected(ParseErr::INCOMPLETE_SUBEXPR);

      // 0 or 1, upper bound
      rules.emplace_back(std::in_place_type_t<Repeat>(), complete.pop_top(), 0,
                         1);
      complete.emplace_top(rules.size() - 1);
      break;
    }
    case '.': {
      // uhhhh gotta figure out what to do about this
      pos_map.emplace_back('!');
      rules.emplace_back(std::in_place_type_t<Any>(), pos_cnt++);
      complete.emplace_top(rules.size() - 1);
      break;
    }
    case '\\': {
      if ((++s_it) >= s_end)
        return std::unexpected(ParseErr::INCOMPLETE_SUBEXPR);

      char c;
      if (*s_it == 'n') {
        c = '\n';
      } else {
        c = *s_it;
      }

      pos_map.emplace_back(c);
      rules.emplace_back(std::in_place_type_t<Char>(), c, pos_cnt++);

      complete.emplace_top(rules.size() - 1);
      break;
    }
    case '\0': {
      // Patterns joined as augmented regexes w/ ((r1)\0)|((r2)\0)|...|((rn)\0)
      // We handle this, not the user, so protect these w/ asserts not errs.
      assert((((s_it + 1) < s_end),
              "final end marker must be followed by a closing parenthesis"));
      assert(((*(s_it + 1) == ')'),
              "end marker must be followed by a closing parenthesis"));
      assert(((((s_it + 2) < s_end) ? (*(s_it + 2) == '|') : true),
              "non-final augmented pattern must be followed by a union"));

      pos_map.emplace_back('\0');
      rules.emplace_back(std::in_place_type_t<EndMarker>(), pos_cnt++);
      complete.emplace_top(rules.size() - 1);
      break;
    }
    default: {
      pos_map.emplace_back(*s_it);
      rules.emplace_back(std::in_place_type_t<Char>(), *s_it, pos_cnt++);
      complete.emplace_top(rules.size() - 1);
      break;
    }
    }
    s_it++;
  }

  if (complete.empty())
    return std::unexpected(ParseErr::EMPTY_EXPR);

  summarize_complete();

  return complete.pop_top();
}

std::expected<std::tuple<size_t, std::vector<Rule>, PositionSet, PositionMap>,
              ParseErr>
parse_expr(std::string_view const s) noexcept {
  std::println("Expr: {}", s);
  std::vector<Rule> rules{};
  PositionMap pos_map{};
  int pos_cnt = 0;
  auto s_it = s.cbegin();

  TRY_USE(root_rule, _parse_expr(rules, pos_map, pos_cnt, s_it, s.cend()));
  assert(pos_cnt >= 0);

  return std::make_tuple(root_rule, std::move(rules), PositionSet(pos_cnt),
                         std::move(pos_map));
}

int main(int argc, char *argv[]) {
  auto res = parse_expr(std::format("(({}){})", argv[1], '\0'));
  if (res.has_value()) {
    auto [root, rules, pos_set, pos_map] = std::move(res).value();

    std::println("Parsed expression:\n{}", rule_lisp_str(rules, root));

    solve_tree(rules, pos_set, root);

    auto [states, accepting_states] =
        build_dfa(rules.at(root), pos_set, pos_map);

    size_t idx = 0;
    for (auto const &state : states) {
      std::println("State {}:", idx);
      char c = 0;
      for (int next : state) {
        if (next >= 0)
          std::println("\t{} -> {}", c, next);
        c++;
      }
      idx++;
    }

    std::println("Accepting states:");
    for (size_t state : accepting_states)
      std::print("{}", state);
    // std::println("followpos table:");
    // for (auto [idx, poses] : std::views::enumerate(pos_set))
    //   std::println(
    //       "{:>2} ({}) | {{{}}}", idx + 1, pos_map.at(idx),
    //       std::ranges::fold_left(poses, "", [](std::string &&s, size_t i) {
    //         return std::format("{} {},", std::move(s), i + 1);
    //       }));

  } else {
    std::println("Failed to parse");
  }
}
