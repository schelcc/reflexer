#include <format>
#include <print>
#include <ranges>

#include "generator.hpp"
#include "rules.hpp"

using Pattern = std::string_view;
struct Language {
  static constexpr Pattern IDENTIFIER = "[a-zA-Z_][a-zA-Z0-9_]*";
  static constexpr Pattern KW_IF = "if";
  static constexpr Pattern KW_WHILE = "while";
  static constexpr Pattern LITERAL_INT = "[-+]?[0-9]+";
  static constexpr Pattern LITERAL_DBL =
      "[-+]?(([0-9]+\.[0-9]*)|([0-9]*\.[0-9]+))";
};

consteval const Reflexer::Generator::DFA *make_language() {
  std::string pattern = "";

  constexpr auto acc_ctx = std::meta::access_context::unchecked();
  constexpr auto members = std::define_static_array(
      std::meta::static_data_members_of(^^Language, acc_ctx));

  template for (constexpr auto m : auto(members)) {
    pattern +=
        ("((" + std::string([:m:]) + ")" + Reflexer::Generator::END_CHAR + ")");
  }

  pattern.pop_back();

  std::unordered_map<std::string, int> foo{};

  auto res = Reflexer::Generator::ParseExpr(pattern);

  return {};

  // Reflexer::Generator::SolveTree(ctx.rules, ctx.pos_set, root);

  // return std::define_static_object<Reflexer::Generator::DFA>(
  //     Reflexer::Generator::BuildDFA(ctx, root));
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

  static constexpr const DFA *dfa_ptr = make_language();

  // DFA dfa = BuildDFA(ctx, root);
  DFA const &dfa = *dfa_ptr;

  std::println("Built DFA");

  size_t idx = 0;
  for (auto const &transitions : dfa.transitions) {
    std::println("State: {}", idx++);
    char c = 0;
    for (int transition : transitions) {
      if (transition >= 0)
        std::println("\t{:?} -> {}", c, transition);
      c++;
    }
  }

  std::println("Accepting states: ");
  for (auto [state, pattern_id] : dfa.accepting_states)
    std::println("\t- state {} marks pattern {}", state, pattern_id);
}
