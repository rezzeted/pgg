#pragma once

// Did-you-mean suggestions for unknown names (E103/E201/E505 diagnostics, the
// params RPC): Levenshtein edit distance plus a prefix rule. The language is
// written by LLM agents, and the typical miss is a typo or a truncation, not
// a random string — both are caught by these two measures.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pgg {

// Levenshtein distance with early cutoff: returns a value greater than `cap`
// when the true distance exceeds it (the exact value is unspecified then).
size_t editDistance(std::string_view a, std::string_view b, size_t cap);

// Up to `cap` candidates closest to `name`. A candidate qualifies when its
// edit distance to `name` is within the length-scaled threshold, or when one
// side is a prefix of the other (truncated typing). Sorted by (distance,
// name) for deterministic output.
std::vector<std::string> suggestNames(std::string_view name, const std::vector<std::string>& candidates,
                                      size_t cap = 3);

// "did you mean 'a'?" / "did you mean 'a', 'b' or 'c'?" — empty when nothing
// qualifies. Ready to use as a Diagnostic hint (or a hint prefix).
std::string didYouMeanHint(std::string_view name, const std::vector<std::string>& candidates, size_t cap = 3);

}  // namespace pgg
