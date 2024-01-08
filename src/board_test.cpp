#include <gtest/gtest.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unordered_set>

#include "blokusduo.h"
#include "piece.h"

namespace blokusduo {
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

template <class Game>
void verify_key(const BoardImpl<Game>& b) {
  typename BoardImpl<Game>::Key key;
  for (int y = 0; y < BoardImpl<Game>::YSIZE; y++) {
    for (int x = 0; x < BoardImpl<Game>::XSIZE; x++) {
      int c = b.at(x, y);
      if (c & BoardImpl<Game>::VIOLET_TILE) key.set(0, x, y);
      if (c & BoardImpl<Game>::ORANGE_TILE) key.set(1, x, y);
    }
  }
  if (b.did_pass(0)) key.set_pass(0);
  if (b.did_pass(1)) key.set_pass(1);
  if (b.player() == 1) key.flip_player();

  EXPECT_EQ(key, b.key());
}

TEST(Move, Move) {
  EXPECT_FALSE(Move().is_valid());
  EXPECT_FALSE(Move().is_pass());
  EXPECT_TRUE(Move::pass().is_pass());
  EXPECT_TRUE(Move::pass().is_valid());

  Move m("56f2");
  EXPECT_EQ(4, m.x());
  EXPECT_EQ(5, m.y());
  EXPECT_EQ('f', m.piece());
  EXPECT_EQ(2, m.orientation());

  EXPECT_EQ("43b2", Move("33b6").canonicalize().code());

  const char* rotates_of_23f3[8] = {
      "23f3", "73f2", "62f1", "32f0", "76f7", "26f6", "37f5", "67f4",
  };
  for (int r = 0; r < 8; r++) {
    EXPECT_EQ(rotates_of_23f3[r],
              mini::Board::rotate_move(Move("23f3"), r).code());
  }

  const char* rotates_of_34t0[8] = {
      "34t0", "C4t1", "B3t2", "43t3", "CBt4", "3Bt5", "4Ct6", "BCt7",
  };
  for (int r = 0; r < 8; r++) {
    EXPECT_EQ(rotates_of_34t0[r],
              standard::Board::rotate_move(Move("34t0"), r).code());
  }
}

TEST(Board, AllPossibleMoves) {
  EXPECT_EQ(1270, mini::Board::all_possible_moves().size());
  EXPECT_EQ(13730, standard::Board::all_possible_moves().size());
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

    verify_key(b);
  }
}

}  // namespace
}  // namespace blokusduo
