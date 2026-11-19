#include <format>
#include <print>

#include "generator.hpp"
#include "lexer.hpp"
#include "rules.hpp"

int main(int argc, char *argv[]) {
  using namespace Reflexer::Generator;

  std::string pattern = "";
  for (size_t i = 1; i < argc; ++i) {
    std::println("Adding pattern {}", argv[i]);
    pattern.append(std::format("(({}){})|", argv[i], END_CHAR));
  }
  pattern.pop_back();

  auto [root, ctx] = ParseExpr(pattern);

  std::println("Full expr: {}", pattern);
  std::println("Parsed expression: \n{}",
               Reflexer::Rules::SexpRepr(ctx.rules, root));

  Reflexer::Rules::Rule &root_rule = SolveTree(ctx.rules, ctx.pos_set, root);
  std::println("Solved tree");

  DFA dfa = BuildDFA(ctx, root);
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
