#ifndef PROBCUT_H_
#define PROBCUT_H_

#include "blokusduo.h"

namespace blokusduo {

constexpr int PROBCUT_MIN_HEIGHT = 3;
constexpr int PROBCUT_MAX_HEIGHT = 10;
constexpr int PROBCUT_MAX_TURN = 24;

struct ProbCut {
  int depth;
  double a, b, sigma;
};

const ProbCut probcut_table[PROBCUT_MAX_TURN + 1][PROBCUT_MAX_HEIGHT] = {
#include "probcut.tab.c"
};

template <class Game>
const ProbCut* probcut_entry(const BoardImpl<Game>& board, int depth);

template <>
const ProbCut* probcut_entry<BlokusDuoMini>(const BoardImpl<BlokusDuoMini>&,
                                            int) {
  return nullptr;
}

template <>
const ProbCut* probcut_entry<BlokusDuoStandard>(
    const BoardImpl<BlokusDuoStandard>& board, int depth) {
  if (depth < PROBCUT_MIN_HEIGHT || depth > PROBCUT_MAX_HEIGHT ||
      board.turn() > PROBCUT_MAX_TURN)
    return nullptr;
  const ProbCut* pc = &probcut_table[board.turn()][depth - PROBCUT_MIN_HEIGHT];
  if (pc->depth == 0) return nullptr;
  return pc;
}

}  // namespace blokusduo

#endif  // PROBCUT_H_
