#include <cstddef>
#include <format>
#include <meta>
#include <print>
#include <ranges>

#include "generator.hpp"
#include "rules.hpp"
#include "tools.hpp"

using Pattern = std::string_view;
struct Language {
  static constexpr Pattern IDENTIFIER = "[a-zA-Z_][a-zA-Z0-9_]*";
  static constexpr Pattern KW_IF = "if";
  static constexpr Pattern KW_WHILE = "while";
  static constexpr Pattern LITERAL_INT = "[-+]?[0-9]+";
  static constexpr Pattern LITERAL_DBL =
      R"~([-+]?([0-9]*\.[0-9]+)|([0-9]+\.))~";
};

consteval Reflexer::Generator::DFAView make_language() {
  constexpr auto acc_ctx = std::meta::access_context::unchecked();
  constexpr auto members = std::define_static_array(
      std::meta::static_data_members_of(^^Language, acc_ctx));

  std::string pattern = "";

  template for (constexpr auto m : auto(members)) {
    pattern += ("((" + std::string([:m:]) + ")" +
                Reflexer::Generator::END_CHAR + ")|");
  }

  pattern.pop_back();

  auto [root, ctx] = Reflexer::Generator::ParseExpr(pattern);

  Reflexer::Generator::SolveTree(ctx.rules, ctx.pos_set, root);
  auto [transitions, acc] = Reflexer::Generator::BuildDFA(ctx, root);

  return {std::define_static_array(std::move(transitions)),
          std::define_static_array(std::move(acc)),
          std::define_static_string(std::move(pattern))};
}

std::optional<size_t> simple_tokenize(std::string const &s,
                                      Reflexer::Generator::DFAView const &dfa) {
  int state = 0;

  for (char c : s) {
    if (state == -1)
      return std::nullopt;

    std::println("\t- in {}, {:?} -> {}", state, c, dfa.transitions[state][c]);

    state = dfa.transitions[state][c];
  }

  if (state == -1)
    return std::nullopt;

  for (auto const &[acc_state, pat_id] : dfa.accepting_states) {
    if (acc_state == state)
      return pat_id;
  }

  return std::nullopt;
}

int main(int argc, char *argv[]) {
  using namespace Reflexer::Generator;

  // std::string pattern = "";
  // for (size_t i = 1; i < argc; ++i) {
  //   std::println("Adding pattern {}", argv[i]);
  //   pattern.append(std::format("(({}){})|", argv[i], END_CHAR));
  // }
  // pattern.pop_back();

  // auto [root, ctx] = ParseExpr(pattern);

  // std::println("Full expr: {}", pattern);
  // std::println("Parsed expression: \n{}",
  //              Reflexer::Rules::SexpRepr(ctx.rules, root));

  // Reflexer::Rules::Rule &root_rule = SolveTree(ctx.rules, ctx.pos_set, root);
  // std::println("Solved tree");

  DFAView dfa_view = make_language();
  std::println("Full pattern: {}", dfa_view.pattern);
  std::println("Num States: {}", dfa_view.transitions.size());

  auto test_input = [&dfa_view](std::string const &s,
                                std::optional<size_t> exp_pat_id) -> bool {
    std::println("-----");
    std::println("Input: {}", s);
    std::print("Expecting: ");
    if (exp_pat_id)
      std::println("Pattern ID {}", *exp_pat_id);
    else
      std::println("Invalid token");

    auto res = simple_tokenize(s, dfa_view);
    std::print("Saw: ");
    if (res)
      std::println("Pattern ID {}", *res);
    else
      std::println("Invalid token");

    return exp_pat_id == res;
  };

  assert(test_input("123456", 3));
  assert(test_input("+123456", 3));
  assert(test_input("-123456", 3));
  assert(test_input("-.123456", 4));
  assert(test_input("+.123456", 4));
  assert(test_input(".123456", 4));
  assert(test_input("123456.", 4));

  // DFA dfa = BuildDFA(ctx, root);

  // std::println("Built DFA");
  return 0;

  size_t idx = 0;
  for (auto const &transitions : dfa_view.transitions) {
    std::println("State: {}", idx++);
    char c = 0;
    for (int transition : transitions) {
      if (transition >= 0)
        std::println("\t{:?} -> {}", c, transition);
      c++;
    }
  }

  std::println("Accepting states: ");
  for (auto [state, pattern_id] : dfa_view.accepting_states)
    std::println("\t- state {} marks pattern {}", state, pattern_id);
}
