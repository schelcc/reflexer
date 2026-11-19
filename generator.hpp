#pragma once

#include <array>
#include <bitset>
#include <cassert>
#include <climits>
#include <cstddef>
#include <deque>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "rules.hpp"

namespace Reflexer::Generator {

/// @brief Maps char value to next state, or -1 if no state can follow
using TransitionMap = std::array<int, 128>;

/// @brief Maps positions in the tree to positions which can follow
using PositionSet = std::vector<std::set<size_t>>;

/// @brief Maps position index to the character in the regex it represents
using PositionMap = std::vector<char>;

/// @brief Character used in the augmented regexes to represent the
/// end of an individual token
static constexpr char END_CHAR = char{0};

/// @brief Character used to represent the '.' "any" character
static constexpr char ANY_CHAR = char{26};

/// @brief Character used to denote the empty string
static constexpr char EMPTY_CHAR = char{21};

namespace PresetCharClass {

consteval std::bitset<128> AlphaLower() {
  std::bitset<128> out{};
  for (char c = 'a'; c <= 'z'; ++c)
    out.set(c);
  return out;
}

consteval std::bitset<128> AlphaUpper() {
  std::bitset<128> out{};
  for (char c = 'A'; c <= 'Z'; ++c)
    out.set(c);
  return out;
}

consteval std::bitset<128> Digits() {
  std::bitset<128> out{};
  for (char c = '0'; c <= '9'; ++c)
    out.set(c);
  return out;
}

consteval std::bitset<128> Whitespace() {
  std::bitset<128> out{};
  out.set(' ');
  out.set('\f');
  out.set('\n');
  out.set('\r');
  out.set('\t');
  out.set('\v');
  return out;
}

}; // namespace PresetCharClass

/// @brief Produce a bitset that clears any non-printable control chars
consteval std::bitset<128> CtrlCharMask() {
  return ~std::bitset<128>{0xFFFFFFFF} | PresetCharClass::Whitespace();
}

/** @brief Represents a state in the DFA as a set of positions. Currently hashes
 * by concatenating positions as strings, delimited by ":", and calculating the
 * string's hash.
 *
 * Later improvement by selecting a different hashing approach would eliminate
 * need for both string and an ordered set. */
struct DFAState {
  std::set<size_t> positions{};

  [[nodiscard]] bool operator==(DFAState const &other) const noexcept {
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

struct DFA {
  std::vector<TransitionMap> transitions;
  std::unordered_map<size_t, size_t> accepting_states; // state -> pattern num
};

}; // namespace Reflexer::Generator

namespace std {
template <> struct hash<Reflexer::Generator::DFAState> {
  inline size_t operator()(Reflexer::Generator::DFAState const &s) const {
    return hash<string>{}(s);
  }
};
}; // namespace std

namespace Reflexer::Generator {

struct ParsingContext {
  std::vector<Rules::Rule> rules{};
  std::vector<std::set<size_t>> pos_set{};
  std::vector<char> pos_map{};
  std::unordered_map<size_t, size_t> end_marker_map{}; // pos. -> pattern num
  int pos_cnt = 0;
};

static inline DFA BuildDFA(ParsingContext const &ctx, size_t const root_ptr) {
  DFA dfa{};

  // The amount of information in DFAState objects is not necessary after
  // construction, so maintain a temporary mapping of DFAStates to transition
  // indices
  std::unordered_map<DFAState, size_t> state_map{};

  std::queue<DFAState> to_visit{};

  auto &root_rule = ctx.rules.at(root_ptr);
  to_visit.emplace(*root_rule.firstpos);
  state_map.emplace(*root_rule.firstpos, 0);

  // Back of dfa.transitions should always be the "current" state
  dfa.transitions.emplace_back();
  dfa.transitions.back().fill(-1);

  while (!to_visit.empty()) {
    DFAState state = to_visit.front();
    to_visit.pop();

    size_t state_idx = state_map.at(state);

    std::unordered_map<char, std::set<size_t>> char_to_pos_set{};

    // Group all positions in this state by their corresponding character
    for (size_t pos : state.positions) {
      char c = ctx.pos_map.at(pos);
      char_to_pos_set.try_emplace(ctx.pos_map.at(pos));
      char_to_pos_set.at(c).emplace(pos);
    }

    auto endmark_it = char_to_pos_set.find(END_CHAR);
    if (endmark_it != char_to_pos_set.end()) {
      // Intuitively this should hold but I don't yet know why. But I
      // rely on it for the next step so protect it.
      assert(endmark_it->second.size() == 1);
      dfa.accepting_states.emplace(
          state_idx, ctx.end_marker_map.at(*endmark_it->second.begin()));
      char_to_pos_set.erase(END_CHAR);
    }

    for (auto [c, positions] : char_to_pos_set) {
      DFAState new_state{};

      for (size_t pos : positions)
        new_state.positions.insert_range(ctx.pos_set.at(pos));

      auto [iter, is_new_state] =
          state_map.try_emplace(new_state, dfa.transitions.size());

      if (is_new_state) {
        dfa.transitions.emplace_back();
        dfa.transitions.back().fill(-1);
        to_visit.emplace(std::move(new_state));
      }

      dfa.transitions.at(state_idx).at(c) = iter->second;
      if (c == ANY_CHAR) {
        auto &trans = dfa.transitions.at(state_idx);
        auto next = iter->second;

        for (char i = 32; i < 127; ++i)
          trans[i] = next;

        // Undo adding a transition on the ANY_CHAR
        trans[ANY_CHAR] = -1;
      }
    }
  }

  return dfa;
}

static inline Rules::Rule &SolveTree(std::vector<Rules::Rule> &rules,
                                     PositionSet &followpos,
                                     size_t const root_ptr) {
  struct Vis {
    Rules::Rule &enclosing_rule;

    std::vector<Rules::Rule> &rules;
    PositionSet &followpos;

    void operator()(Rules::Union &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos.emplace();
      enclosing_rule.lastpos.emplace();

      for (size_t child_idx : r.children) {
        Rules::Rule child_rule = SolveTree(rules, followpos, child_idx);
        enclosing_rule.nullable =
            *enclosing_rule.nullable || *child_rule.nullable;
        enclosing_rule.firstpos->insert_range(*child_rule.firstpos);
        enclosing_rule.lastpos->insert_range(*child_rule.lastpos);
      }
    }
    void operator()(Rules::Concat &r) {

      assert(r.left_child.has_value() && r.right_child.has_value());

      Rules::Rule &left = SolveTree(rules, followpos, *r.left_child);
      Rules::Rule &right = SolveTree(rules, followpos, *r.right_child);

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
    void operator()(Rules::Repeat &r) {
      // For now, only going to handle kleene (min 0, max null)
      assert(r.child.has_value() && !r.max_repeat.has_value());

      Rules::Rule &child = SolveTree(rules, followpos, *r.child);

      assert(!child.firstpos->empty());
      assert(!child.lastpos->empty());

      enclosing_rule.nullable = (r.min_repeat == 0);
      enclosing_rule.firstpos = *child.firstpos;
      enclosing_rule.lastpos = *child.lastpos;

      for (size_t i : *child.lastpos)
        followpos.at(i).insert_range(*enclosing_rule.firstpos);
    }
    void operator()(Rules::Char &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
    void operator()(Rules::Any &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
    void operator()(Rules::EmptyPos &r) {
      enclosing_rule.nullable = true;
      enclosing_rule.firstpos.emplace();
      enclosing_rule.lastpos.emplace();
    }
    void operator()(Rules::EndMarker &r) {
      enclosing_rule.nullable = false;
      enclosing_rule.firstpos = {r.pos};
      enclosing_rule.lastpos = {r.pos};
    }
  };

  Rules::Rule &rule = rules.at(root_ptr);
  std::visit(Vis{rule, rules, followpos}, rule);
  return rule;
}

struct CharRes {
  char c;
  bool escaped;
};

static inline CharRes ProcChar(std::string_view::const_iterator const &s_it,
                               std::string_view::const_iterator const &s_end) {
  // Reads current
  if (s_it >= s_end)
    throw std::invalid_argument("Tried to process char past the end of input");

  if (*s_it == '\\') {
    if ((s_it + 1) >= s_end)
      throw std::invalid_argument(
          "Tried to process escaped char past the end of input");

    const char c = *(s_it + 1);
    switch (c) {
    case 'n':
      return {'\n', true};
    case 'r':
      return {'\r', true};
    case 't':
      return {'\t', true};
    default:
      return {c, true};
    }
  } else {
    return {*s_it, false};
  }
}

static inline CharRes
ProcAdvance(std::string_view::const_iterator &s_it,
            std::string_view::const_iterator const &s_end) {
  // Reads current, advances just after current
  CharRes res = ProcChar(s_it, s_end);
  s_it += (1 + res.escaped);
  return res;
}

static inline size_t ParseSubExpr(ParsingContext &ctx,
                                  std::string_view::const_iterator &s_it,
                                  std::string_view::const_iterator const &s_end,
                                  int group_depth = 0) {
  std::deque<size_t> complete{};

  // Procedure to bottom-to-top concatenate rules, leaving just one rule on the
  // complete stack
  auto summarize_complete = [&] {
    while (complete.size() > 1) {
      size_t left_expr = complete.front();
      complete.pop_front();

      size_t right_expr = complete.front();
      complete.pop_front();

      ctx.rules.emplace_back(std::in_place_type_t<Rules::Concat>(), left_expr,
                             right_expr);
      complete.emplace_front(ctx.rules.size() - 1);
    }
  };

  // Procedure to build and add the rules for a bitset representing a char class
  auto add_char_class_rules = [&](std::bitset<128> &&bs) {
    // Clear positions for control chars that shouldn't be allowed (all other
    // than \n, \r, \t)
    bs &= CtrlCharMask();

    Rules::Union new_rule{};

    for (char c = 0; c < 127; ++c) {
      if (bs.test(c)) {
        ctx.pos_map.emplace_back(c);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Char>(), c,
                               ctx.pos_cnt++);
        new_rule.children.emplace_back(ctx.rules.size() - 1);
      }
    }

    ctx.rules.emplace_back(std::move(new_rule));
    complete.emplace_back(ctx.rules.size() - 1);
  };

  while (s_it < s_end) {
    auto [cur_char, escaped] = ProcAdvance(s_it, s_end);
    if (escaped) {
      switch (cur_char) {
        using namespace PresetCharClass;
      case 'd': {
        add_char_class_rules(Digits());
      } break;
      case 'D': {
        add_char_class_rules(~Digits());
      } break;
      case 'w': {
        add_char_class_rules(AlphaLower() | AlphaUpper() | Digits());
      } break;
      case 'W': {
        add_char_class_rules(~(AlphaLower() | AlphaUpper() | Digits()));
      } break;
      case 'l': {
        add_char_class_rules(AlphaLower() | AlphaUpper());
      } break;
      case 'L': {
        add_char_class_rules(~(AlphaLower() | AlphaUpper()));
      } break;
      case 's': {
        add_char_class_rules(Whitespace());
      } break;
      case 'S': {
        add_char_class_rules(~Whitespace());
      } break;
      default: {
        ctx.pos_map.emplace_back(cur_char);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Char>(), cur_char,
                               ctx.pos_cnt++);
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      }
    } else {
      switch (cur_char) {
      case '(':
        complete.emplace_back(ParseSubExpr(ctx, s_it, s_end, group_depth + 1));
        break;
      case ')': {
        if (group_depth == 0)
          throw std::invalid_argument(
              "Closing parenthesis without corresponding open parenthesis");
        summarize_complete();
        return complete.back();
      } break;
      case '|': {
        if (complete.empty() || (s_it >= s_end))
          throw std::invalid_argument("Regex-or operator missing an operand");

        summarize_complete();

        size_t left_expr = complete.back();
        complete.pop_back();

        ctx.rules.emplace_back(std::in_place_type_t<Rules::Union>(), left_expr,
                               ParseSubExpr(ctx, s_it, s_end, group_depth));
        return ctx.rules.size() - 1;
      } break;
      case '*': {
        if (complete.empty())
          throw std::invalid_argument(
              "Regex zero-or-more operator missing operand");

        ctx.rules.emplace_back(std::in_place_type_t<Rules::Repeat>(),
                               complete.back());
        complete.pop_back();
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      case '+': {
        // Concatenation of "<pattern>" and "<pattern>*"
        if (complete.empty())
          throw std::invalid_argument(
              "Regex zero-or-more operator missing operand");

        ctx.rules.emplace_back(std::in_place_type_t<Rules::Repeat>(),
                               complete.back());
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Concat>(),
                               complete.back(), ctx.rules.size() - 1);
        complete.pop_back();
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      case '?': {
        // Union of "<pattern>" and ""
        if (complete.empty())
          throw std::invalid_argument(
              "Regex zero-or-more operator missing operand");

        size_t nullable_pat = complete.back();
        complete.pop_back();

        ctx.pos_map.emplace_back(EMPTY_CHAR);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::EmptyPos>(),
                               ctx.pos_cnt++);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Union>(),
                               ctx.rules.size() - 1, nullable_pat);
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      case '.': {
        ctx.pos_map.emplace_back(ANY_CHAR);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Any>(),
                               ctx.pos_cnt++);
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      case '[': {
        if (s_it >= s_end)
          throw std::invalid_argument("Unclosed character class");
        else if (*s_it == ']')
          throw std::invalid_argument("Empty character class is not allowed");

        CharRes res;

        res = ProcAdvance(s_it, s_end);
        const bool inverted = (!res.escaped && (res.c == '^'));

        // Need to step back s_it if not inverted, else we skip it
        s_it -= inverted ? 0 : 1;

        // Track a bitset of the allowed characters, then add them as
        // rules once completely done so that negation is easily handled
        std::bitset<128> included_chars{};

        std::optional<CharRes> last_res = std::nullopt;
        CharRes cur_res;

        while ((s_it < s_end) && (*s_it != ']')) {
          cur_res = ProcAdvance(s_it, s_end);

          if (!last_res) {
            last_res = cur_res;
            continue;
          }

          if (!cur_res.escaped && (cur_res.c == '-')) {
            auto [next_char, next_escaped] = ProcAdvance(s_it, s_end);

            if (last_res->escaped || next_escaped)
              throw std::invalid_argument(
                  "Escaped characters not allowed in character-class range");

            if (last_res->c > next_char)
              throw std::invalid_argument(
                  "Negative character-class range is not allowed");

            for (char c = last_res->c; c <= next_char; ++c)
              included_chars.set(c);

            last_res = std::nullopt;
          } else {
            included_chars.set(last_res->c);
            last_res = cur_res;
          }
        }

        if (*s_it != ']')
          throw std::invalid_argument("Unclosed character class");

        // Move off of ']'
        s_it++;

        if (last_res)
          included_chars.set(last_res->c);

        included_chars = inverted ? ~included_chars : included_chars;
        add_char_class_rules(std::move(included_chars));
      } break;
      case END_CHAR: {
        // Patterns joined as augmented regexes w/
        // ((r1)\0)|((r2)\0)|...|((rn)\0) We handle this, not the user, so
        // protect these w/ asserts not errs.
        assert(((s_it < s_end),
                "final end marker must be followed by a closing parenthesis"));

        assert(((*s_it == ')'),
                "end marker must be followed by a closing parenthesis"));

        assert(((((s_it + 1) < s_end) ? (*(s_it + 1) == '|') : true),
                "non-final augmented pattern must be followed by a union"));

        ctx.pos_map.emplace_back(END_CHAR);
        ctx.end_marker_map.emplace(ctx.pos_cnt, ctx.end_marker_map.size());

        ctx.rules.emplace_back(std::in_place_type_t<Rules::EndMarker>(),
                               ctx.pos_cnt++);
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      default: {
        ctx.pos_map.emplace_back(cur_char);
        ctx.rules.emplace_back(std::in_place_type_t<Rules::Char>(), cur_char,
                               ctx.pos_cnt++);
        complete.emplace_back(ctx.rules.size() - 1);
      } break;
      }
    }
  }

  if (complete.empty())
    throw std::invalid_argument("Empty expression");

  summarize_complete();
  return complete.back();
}

static inline std::pair<size_t, ParsingContext>
ParseExpr(std::string_view const s) {
  ParsingContext ctx{};
  auto s_it = s.cbegin();

  size_t root_rule = ParseSubExpr(ctx, s_it, s.cend());

  ctx.pos_set = PositionSet(ctx.pos_cnt);

  return {root_rule, std::move(ctx)};
}

}; // namespace Reflexer::Generator
