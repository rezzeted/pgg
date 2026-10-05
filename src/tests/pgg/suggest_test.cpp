// Did-you-mean helpers (suggest.h): edit distance with cutoff, candidate
// ranking (distance, then alpha), prefix rule for truncated typing, and the
// ready-made hint text.
#include <gtest/gtest.h>

#include "pgg/src/eval/suggest.h"

namespace {

TEST(Suggest, EditDistanceBasics) {
    EXPECT_EQ(pgg::editDistance("spool", "spool", 3), 0u);
    EXPECT_EQ(pgg::editDistance("spol", "spool", 3), 1u);
    EXPECT_EQ(pgg::editDistance("selct", "select", 3), 1u);
    EXPECT_EQ(pgg::editDistance("abc", "xyz", 3), 3u);
    // Cutoff: anything farther than cap collapses to a value > cap.
    EXPECT_GT(pgg::editDistance("aaa", "bbbbbbbbbb", 3), 3u);
    EXPECT_GT(pgg::editDistance("short", "a_completely_different_long_name", 3), 3u);
}

TEST(Suggest, TypoBeatsAlphabet) {
    const std::vector<std::string> cands = {"apple", "apply", "spool", "spooler"};
    const std::vector<std::string> near = pgg::suggestNames("spol", cands);
    ASSERT_FALSE(near.empty());
    EXPECT_EQ(near[0], "spool");
}

TEST(Suggest, PrefixRuleCatchesTruncation) {
    const std::vector<std::string> cands = {"arc_shell", "arc_window", "box"};
    const std::vector<std::string> near = pgg::suggestNames("arc_she", cands);
    ASSERT_FALSE(near.empty());
    EXPECT_EQ(near[0], "arc_shell");
    // Over-typed name (candidate is a prefix of the name) qualifies too.
    const std::vector<std::string> near2 = pgg::suggestNames("rocks", {"rock"});
    ASSERT_EQ(near2.size(), 1u);
    EXPECT_EQ(near2[0], "rock");
}

TEST(Suggest, NoiseGetsNoSuggestion) {
    const std::vector<std::string> cands = {"spool", "merge", "raycast"};
    EXPECT_TRUE(pgg::suggestNames("zzqq", cands).empty());
    EXPECT_TRUE(pgg::suggestNames("", cands).empty());
}

TEST(Suggest, CapAndDeterministicOrder) {
    const std::vector<std::string> cands = {"aab", "aaa", "aac", "aad"};
    const std::vector<std::string> near = pgg::suggestNames("aae", cands, 2);
    ASSERT_EQ(near.size(), 2u);
    EXPECT_EQ(near[0], "aaa");  // same distance, alphabetical
    EXPECT_EQ(near[1], "aab");
    // Exact matches are never suggested back.
    EXPECT_TRUE(pgg::suggestNames("aaa", {"aaa"}).empty());
}

TEST(Suggest, HintText) {
    EXPECT_EQ(pgg::didYouMeanHint("spol", {"spool"}), "did you mean 'spool'?");
    EXPECT_EQ(pgg::didYouMeanHint("aae", {"aab", "aaa"}, 2), "did you mean 'aaa' or 'aab'?");
    EXPECT_TRUE(pgg::didYouMeanHint("zzqq", {"spool"}).empty());
}

}  // namespace
