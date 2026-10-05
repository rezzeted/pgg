#include "../../pch.h"

#include "suggest.h"

#include <algorithm>

namespace pgg {

size_t editDistance(std::string_view a, std::string_view b, size_t cap) {
    if (a.size() > b.size()) std::swap(a, b);
    if (b.size() - a.size() > cap) return cap + 1;
    // One DP row over the shorter word; bail out as soon as every cell of a
    // row exceeds the cap (distances can only grow from there).
    std::vector<size_t> prev(a.size() + 1), cur(a.size() + 1);
    for (size_t j = 0; j <= a.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= b.size(); ++i) {
        cur[0] = i;
        size_t rowMin = cur[0];
        for (size_t j = 1; j <= a.size(); ++j) {
            const size_t sub = prev[j - 1] + (a[j - 1] == b[i - 1] ? 0 : 1);
            cur[j] = std::min({sub, prev[j] + 1, cur[j - 1] + 1});
            rowMin = std::min(rowMin, cur[j]);
        }
        if (rowMin > cap) return cap + 1;
        std::swap(prev, cur);
    }
    return prev[a.size()];
}

namespace {

// Longer names tolerate more edits: a one-letter slip is unrecoverable noise
// in "mud" but a clear signal in "distribute_points".
size_t maxDistanceFor(size_t nameLen) {
    if (nameLen <= 4) return 1;
    if (nameLen <= 9) return 2;
    return 3;
}

bool prefixRelated(std::string_view name, std::string_view cand) {
    if (name.size() >= 3 && cand.rfind(name, 0) == 0) return true;  // truncated candidate
    if (cand.size() >= 3 && name.rfind(cand, 0) == 0) return true;  // over-typed candidate
    return false;
}

}  // namespace

std::vector<std::string> suggestNames(std::string_view name, const std::vector<std::string>& candidates,
                                      size_t cap) {
    std::vector<std::string> out;
    if (name.empty() || cap == 0) return out;
    const size_t maxDist = maxDistanceFor(name.size());
    std::vector<std::pair<size_t, std::string>> scored;
    for (const std::string& c : candidates) {
        if (c == name) continue;
        const size_t d = editDistance(name, c, maxDist);
        if (d <= maxDist || prefixRelated(name, c)) scored.emplace_back(d, c);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& x, const auto& y) {
        return x.first != y.first ? x.first < y.first : x.second < y.second;
    });
    for (const auto& [d, c] : scored) {
        out.push_back(c);
        if (out.size() >= cap) break;
    }
    return out;
}

std::string didYouMeanHint(std::string_view name, const std::vector<std::string>& candidates, size_t cap) {
    const std::vector<std::string> near = suggestNames(name, candidates, cap);
    if (near.empty()) return {};
    std::string h = "did you mean ";
    for (size_t i = 0; i < near.size(); ++i) {
        if (i > 0) h += (i + 1 == near.size()) ? " or " : ", ";
        h += "'" + near[i] + "'";
    }
    return h + "?";
}

}  // namespace pgg
