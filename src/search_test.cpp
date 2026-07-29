#include <limits.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <gtest/gtest.h>

#include "blokusduo.h"

namespace blokusduo::search {
namespace {

int depth_two_score(const mini::Board& board, Move move) {
  const mini::Board child = board.child(move);
  int score = INT_MAX;
  for (Move reply : child.valid_moves())
    score = std::min(score, child.child(reply).nega_eval());
  return score;
}

TEST(NegaScoutGumbel, ZeroTemperatureMatchesNegaScout) {
  mini::Board board;
  const auto callback = [](int, SearchResult) { return true; };
  EXPECT_EQ(negascout(board, 3, callback),
            negascout_gumbel(board, 3, 0, 1234, callback));
}

TEST(NegaScoutGumbel, SelectsGumbelPerturbedBestMove) {
  mini::Board board;
  std::vector<SearchResult> scores;
  for (Move move : board.valid_moves())
    scores.emplace_back(move, depth_two_score(board, move));

  for (double temperature : {1e-12, 4.0, 1e6}) {
    for (uint64_t seed = 0; seed < 16; seed++) {
      SCOPED_TRACE(testing::Message()
                   << "temperature=" << temperature << ", seed=" << seed);
      std::mt19937_64 random(seed);
      Move expected_move;
      int expected_score = 0;
      double expected_noise = 0;
      bool found_expected = false;
      for (const auto& [move, score] : scores) {
        constexpr double SCALE = 1.0 / 9007199254740992.0;
        const double uniform =
            (static_cast<double>(random() >> 11) + 0.5) * SCALE;
        const double noise = -temperature * std::log(-std::log(uniform));
        const double score_difference =
            static_cast<double>(score) - expected_score;
        if (!found_expected || score_difference > expected_noise - noise) {
          found_expected = true;
          expected_move = move;
          expected_score = score;
          expected_noise = noise;
        }
      }

      const SearchResult actual = negascout_gumbel(
          board, 2, temperature, seed, [](int, SearchResult) { return true; });
      EXPECT_EQ(expected_move, actual.first);
      EXPECT_EQ(expected_score, actual.second);
    }
  }
}

TEST(NegaScoutGumbel, SeedIsReproducible) {
  mini::Board board;
  const auto callback = [](int, SearchResult) { return true; };
  EXPECT_EQ(negascout_gumbel(board, 3, 4, 5678, callback),
            negascout_gumbel(board, 3, 4, 5678, callback));
}

}  // namespace
}  // namespace blokusduo::search
