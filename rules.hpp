#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <meta>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Reflexer::Rules {
struct Char {
  char rule;
  size_t pos;
};

struct Union {
  std::vector<size_t> children{};

  Union() = default;

  Union(size_t l) : children({l}) {}
  Union(size_t l, size_t r) : children({l, r}) {}
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

struct EmptyPos {
  size_t pos;
};

struct EndMarker {
  size_t pos;
};

using RuleTypes =
    std::variant<Char, Union, Concat, Repeat, Any, EmptyPos, EndMarker>;

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

/** @brief Return the rule tree rooted at `rule_ptr` represented as a collection
 * of pseudo-s-expressions. */
std::string SexpRepr(std::vector<Rule> const &rules, size_t const rule_ptr,
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
                SexpRepr(rules, r.[:m:].value(), prefix + "  ")));
          else
            child_members.append(std::format("\n {}({}: ())", prefix,
                                             std::meta::identifier_of(m)));
        } else if constexpr (std::meta::display_string_of(m).ends_with(
                                 "children")) {
          for (size_t child_idx : r.[:m:])
            child_members.append(std::format(
                "\n {}({}: {})", prefix, std::meta::identifier_of(m),
                SexpRepr(rules, child_idx, prefix + "  ")));
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

}; // namespace Reflexer::Rules
