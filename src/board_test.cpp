#include <gtest/gtest.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <iostream>
#include <queue>
#include <random>
#include <unordered_set>

#include "blokusduo.h"
#include "piece.h"

namespace blokusduo {

std::ostream& operator<<(std::ostream& os, const Move& m) {
  return os << m.code();
}

namespace {

template <class Game>
class MoveCollector : public BoardImpl<Game>::MoveVisitor {
 public:
  virtual bool visit_move(Move m) {
    valid_moves.insert(m);
    return true;
  }
  std::unordered_set<Move, Move::Hash> valid_moves;
};

class InspectableInfluenceBoard : public standard::Board {
 public:
  int influence() const { return eval_influence(); }
};

int reference_standard_influence(const standard::Board& board) {
  int influence[2] = {};
  constexpr int dx[] = {-1, 1, 0, 0};
  constexpr int dy[] = {0, 0, -1, 1};
  for (int player = 0; player < 2; player++) {
    bool blocked[standard::Board::YSIZE][standard::Board::XSIZE] = {};
    bool has_tiles = false;
    for (int y = 0; y < standard::Board::YSIZE; y++) {
      for (int x = 0; x < standard::Board::XSIZE; x++) {
        has_tiles |= board.has_tile(player, x, y);
        blocked[y][x] =
            board.has_tile(player, x, y) ||
            board.has_tile(1 - player, x, y) ||
            (x > 0 && board.has_tile(player, x - 1, y)) ||
            (x + 1 < standard::Board::XSIZE &&
             board.has_tile(player, x + 1, y)) ||
            (y > 0 && board.has_tile(player, x, y - 1)) ||
            (y + 1 < standard::Board::YSIZE &&
             board.has_tile(player, x, y + 1));
      }
    }
    bool reached[standard::Board::YSIZE][standard::Board::XSIZE] = {};
    std::queue<std::array<int, 3>> queue;

    for (int y = 0; y < standard::Board::YSIZE; y++) {
      for (int x = 0; x < standard::Board::XSIZE; x++) {
        const bool corner =
            (x > 0 && y > 0 && board.has_tile(player, x - 1, y - 1)) ||
            (x + 1 < standard::Board::XSIZE && y > 0 &&
             board.has_tile(player, x + 1, y - 1)) ||
            (x > 0 && y + 1 < standard::Board::YSIZE &&
             board.has_tile(player, x - 1, y + 1)) ||
            (x + 1 < standard::Board::XSIZE &&
             y + 1 < standard::Board::YSIZE &&
             board.has_tile(player, x + 1, y + 1));
        const int start_x = player == 0 ? BlokusDuoStandard::START1X
                                        : BlokusDuoStandard::START2X;
        const int start_y = player == 0 ? BlokusDuoStandard::START1Y
                                        : BlokusDuoStandard::START2Y;
        if (!blocked[y][x] &&
            (corner || (!has_tiles && x == start_x && y == start_y))) {
          reached[y][x] = true;
          queue.push({x, y, 0});
          influence[player]++;
        }
      }
    }
    while (!queue.empty()) {
      const auto [x, y, distance] = queue.front();
      queue.pop();
      if (distance == 3) continue;
      for (int direction = 0; direction < 4; direction++) {
        const int next_x = x + dx[direction];
        const int next_y = y + dy[direction];
        if (next_x < 0 || next_y < 0 ||
            next_x >= standard::Board::XSIZE ||
            next_y >= standard::Board::YSIZE || reached[next_y][next_x] ||
            blocked[next_y][next_x])
          continue;
        reached[next_y][next_x] = true;
        queue.push({next_x, next_y, distance + 1});
        influence[player]++;
      }
    }
  }
  return influence[0] - influence[1];
}

class InspectablePieceBoard : public standard::Board {
 public:
  int piece_evaluation() const { return piece_eval_; }
};

int reference_piece_evaluation(const standard::Board& board) {
  constexpr int piece_values[] = {
      2,  4,  6,  6,  10, 10, 10, 10, 10, 16, 16,
      16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  };
  int score = 0;
  for (int piece = 0; piece < BlokusDuoStandard::NUM_PIECES; piece++) {
    if (board.is_piece_available(0, piece)) score -= piece_values[piece];
    if (board.is_piece_available(1, piece)) score += piece_values[piece];
  }
  return score;
}

TEST(Move, Move) {
  EXPECT_FALSE(Move().is_valid());
  EXPECT_FALSE(Move().is_pass());

  EXPECT_TRUE(Move::pass().is_pass());
  EXPECT_TRUE(Move::pass().is_valid());
  EXPECT_TRUE(Move("0000").is_pass());
  EXPECT_TRUE(Move("----").is_pass());
  EXPECT_EQ("0000", Move::pass().code());

  EXPECT_FALSE(Move("").is_valid());
  EXPECT_FALSE(Move("00a0").is_valid());
  EXPECT_FALSE(Move("1234").is_valid());
  EXPECT_FALSE(Move("11a9").is_valid());
  EXPECT_EQ("ccf0", Move("CCF0").code());

  Move m("56f2");
  EXPECT_TRUE(m.is_valid());
  EXPECT_FALSE(m.is_pass());
  EXPECT_EQ(4, m.x());
  EXPECT_EQ(5, m.y());
  EXPECT_EQ('f', m.piece());
  EXPECT_EQ(2, m.orientation());

  EXPECT_EQ(Move("43b2"), Move("33b6").canonicalize());

  const char* rotates_of_23f3[8] = {
      "23f3", "73f2", "62f1", "32f0", "76f7", "26f6", "37f5", "67f4",
  };
  for (int r = 0; r < 8; r++) {
    EXPECT_EQ(Move(rotates_of_23f3[r]),
              mini::Board::rotate_move(Move("23f3"), r));
  }

  const char* rotates_of_34t0[8] = {
      "34t0", "C4t1", "B3t2", "43t3", "CBt4", "3Bt5", "4Ct6", "BCt7",
  };
  for (int r = 0; r < 8; r++) {
    EXPECT_EQ(Move(rotates_of_34t0[r]),
              standard::Board::rotate_move(Move("34t0"), r));
  }
}

TEST(Board, AllPossibleMoves) {
  EXPECT_EQ(1270, mini::Board::all_possible_moves().size());
  EXPECT_EQ(13730, standard::Board::all_possible_moves().size());
}

TEST(Board, OptimizedStandardEvaluationMatchesReference) {
  std::mt19937 random(20260726);
  for (int game = 0; game < 20; game++) {
    InspectableInfluenceBoard board;
    while (!board.is_game_over()) {
      EXPECT_EQ(reference_standard_influence(board), board.influence());
      const std::vector<Move> moves = board.valid_moves();
      board.play_move(moves[random() % moves.size()]);
    }
    EXPECT_EQ(reference_standard_influence(board), board.influence());
  }
}

TEST(Board, CachedPieceEvaluationMatchesReference) {
  std::mt19937 random(20260726);
  for (int game = 0; game < 20; game++) {
    InspectablePieceBoard board;
    while (!board.is_game_over()) {
      EXPECT_EQ(reference_piece_evaluation(board), board.piece_evaluation());
      const std::vector<Move> moves = board.valid_moves();
      board.play_move(moves[random() % moves.size()]);
    }
    EXPECT_EQ(reference_piece_evaluation(board), board.piece_evaluation());
  }
}

template <typename T>
class BoardTest : public testing::Test {
  using Game = T;
};

using Games = ::testing::Types<BlokusDuoMini, BlokusDuoStandard>;
TYPED_TEST_SUITE(BoardTest, Games);

TYPED_TEST(BoardTest, RandomPlayout) {
  srand(time(nullptr));
  BoardImpl<TypeParam> b;
  while (!b.is_game_over()) {
    MoveCollector<TypeParam> collector;
    b.visit_moves(&collector);
    std::unordered_set<Move, Move::Hash> valid_moves =
        std::move(collector.valid_moves);

    ASSERT_FALSE(valid_moves.empty());
    if (valid_moves.contains(Move::pass())) {
      EXPECT_EQ(1, valid_moves.size());
    }

    // Verify that all valid moves are placeable.
    for (auto p : TypeParam::piece_set) {
      for (int y = 0; y < BoardImpl<TypeParam>::YSIZE; y++) {
        for (int x = 0; x < BoardImpl<TypeParam>::XSIZE; x++) {
          Move m = Move(x, y, p->id);
          EXPECT_EQ(b.is_valid_move(m), valid_moves.contains(m));
        }
      }
    }

    // Randomly choose a move and play it.
    int n = rand() % valid_moves.size();
    auto it = valid_moves.begin();
    std::advance(it, n);
    Move m = *it;
    b.play_move(m);
    // printf("%d %s\n", b.turn(), m.code().c_str());
  }
}

}  // namespace
}  // namespace blokusduo
