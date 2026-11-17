#include <format>
#include <print>

#include "generator.hpp"
#include "rules.hpp"

int main(int argc, char *argv[]) {
  using namespace Reflexer::Generator;

  auto [root, ctx] = ParseExpr(std::format("(({}){})", argv[1], END_CHAR));

  std::println("Parsed expression: \n{}",
               Reflexer::Rules::SexpRepr(ctx.rules, root));

  Reflexer::Rules::Rule &root_rule = SolveTree(ctx.rules, ctx.pos_set, root);
  std::println("Solved tree");

  DFA dfa = BuildDFA(root_rule, ctx.pos_set, ctx.pos_map);
  std::println("Built DFA");

  size_t idx = 0;
  for (auto const &transitions : dfa.transitions) {
    std::println("State: {}", idx++);
    char c = 0;
    for (int transition : transitions) {
      if (transition >= 0)
        std::println("\t{} -> {}", c, transition);
      c++;
    }
  }

  std::println("Accepting states: ");
  for (size_t state : dfa.accepting_states)
    std::print("{} ", state);
}
