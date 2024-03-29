#include <stdio.h>
#include <time.h>

#include "blokusduo.h"

namespace blokusduo::search {
namespace {

template <class Game>
Move search_move(const BoardImpl<Game>& b);

template <>
Move search_move(const BoardImpl<BlokusDuoMini>& b) {
  Move move = opening_move(b);
  if (move.is_valid()) return move;
  SearchResult r;
  if (b.turn() < 5)
    r = negascout(b, b.turn() + 3, [](int, SearchResult) { return true; });
  else if (b.turn() < 7)
    r = wld(b);
  else
    r = perfect(b);
  return r.first;
}

template <>
Move search_move(const BoardImpl<BlokusDuoStandard>& b) {
  int max_depth = b.turn() < 10 ? 3 : b.turn() < 16 ? 4 : b.turn() < 20 ? 5 : 6;

  Move move = opening_move(b);
  if (move.is_valid()) return move;
  SearchResult r;
  if (b.turn() < 21)
    r = negascout(b, max_depth, [](int, SearchResult) { return true; });
  else if (b.turn() < 25)
    r = wld(b);
  else
    r = perfect(b);
  return r.first;
}

}  // namespace

template <class Game>
void playout() {
  BoardImpl<Game> b;
  while (!b.is_game_over()) {
    clock_t start = clock();
    visited_nodes = 0;

    Move m = search_move(b);
    b.play_move(m);

    double sec = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("%d %s %d nodes / %.3f sec (%d nps)\n", b.turn(), m.code().c_str(),
           visited_nodes, sec, (int)(visited_nodes / sec));
    fflush(stdout);
  }
  printf("Final score: %d - %d\n", b.score(0), b.score(1));
}

}  // namespace blokusduo::search

int main() {
  blokusduo::search::playout<blokusduo::BlokusDuoMini>();
  blokusduo::search::playout<blokusduo::BlokusDuoStandard>();
  return 0;
}
