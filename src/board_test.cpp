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

void fail(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  exit(1);
}

template <class Game>
void verify_key(const BoardImpl<Game> &b) {
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

  if (key != b.key()) {
    fail("Key mismatch\n");
  }
}

}  // namespace

void test_move() {
  Move m("56f2");
  if (m.x() != 4 || m.y() != 5 || m.piece() != 'f' || m.orientation() != 2) {
    fail("56f2 should be (4, 5, 'f', 2), but got (%d, %d, '%c', %d)\n", m.x(),
         m.y(), m.piece(), m.orientation());
  }
  Move canonical = Move("33b6").canonicalize();
  if (canonical != Move("43b2")) {
    fail("33b6 should be canonicalized to 43b2, but got %s\n",
         canonical.fourcc().c_str());
  }
  if (m.mirror() != Move("65f5")) {
    fail("mirror of 56f2 should be 65f5, but got %s\n",
         m.mirror().fourcc().c_str());
  }
}

template <class Game>
void test_random_playout() {
  BoardImpl<Game> b;
  while (!b.is_game_over()) {
    MoveCollector<Game> collector;
    b.visit_moves(&collector);
    std::unordered_set<Move, Move::Hash> valid_moves =
        std::move(collector.valid_moves);

    if (valid_moves.empty()) {
      fail("No leval moves\n");
    } else if (valid_moves.find(Move::pass()) != valid_moves.end()) {
      if (valid_moves.size() != 1) fail("PASS is not the only valid move\n");
    }

    // Verify that all valid moves are placeable.
    for (auto p : Game::piece_set) {
      for (int y = 0; y < BoardImpl<Game>::YSIZE; y++) {
        for (int x = 0; x < BoardImpl<Game>::XSIZE; x++) {
          Move m = Move(x, y, p->id);
          bool placeable = b.is_valid_move(m);
          bool found = valid_moves.find(m) != valid_moves.end();
          if (placeable && !found)
            fail("%s is placeable but not found\n", m.fourcc().c_str());
          else if (!placeable && found)
            fail("%s is not placeable but found\n", m.fourcc().c_str());
        }
      }
    }

    // Randomly choose a move and play it.
    int n = rand() % valid_moves.size();
    auto it = valid_moves.begin();
    std::advance(it, n);
    Move m = *it;
    b.play_move(m);
    // printf("%d %s\n", b.turn(), m.fourcc().c_str());

    verify_key(b);
  }
}

template <class Game>
void test_all_possible_moves(size_t expected_num_moves) {
  std::vector<Move> moves = BoardImpl<Game>::all_possible_moves();
  if (moves.size() != expected_num_moves) {
    fail("Expected %zu moves but got %zu\n", expected_num_moves, moves.size());
  }
}

}  // namespace blokusduo

int main() {
  srand(time(nullptr));
  blokusduo::test_move();
  blokusduo::test_random_playout<blokusduo::BlokusDuoMini>();
  blokusduo::test_random_playout<blokusduo::BlokusDuoStandard>();
  blokusduo::test_all_possible_moves<blokusduo::BlokusDuoMini>(1270);
  blokusduo::test_all_possible_moves<blokusduo::BlokusDuoStandard>(13730);
  return 0;
}
