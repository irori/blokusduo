#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#include "blokusduo.h"
#include "piece.h"

#define USE_PROBCUT
#undef PROBSTAT

#ifdef USE_PROBCUT
#include "probcut.h"
#endif

namespace blokusduo::search {

int visited_nodes;

namespace {

#define CHECKPOINT_INTERVAL 10000

static int check_point;
static clock_t expire_clock;
static bool enable_timeout;
static bool timed_out;

template <class Game>
using Hash =
    std::unordered_map<typename BoardImpl<Game>::Key, std::pair<int, int>,
                       typename BoardImpl<Game>::Key::Hash>;

template <class Game>
struct Child {
  Child(const BoardImpl<Game>& b, Move m, Hash<Game>* hash);
  bool operator<(const Child& rhs) const { return score < rhs.score; }
  BoardImpl<Game> board;
  int score;
  Move move;
};

template <class Game>
Child<Game>::Child(const BoardImpl<Game>& b, Move m, Hash<Game>* hash)
    : board(b.child(m)), move(m) {
  auto i = hash->find(board.key());
  if (i != hash->end()) {
    int a = i->second.first;
    int b = i->second.second;
    if (a > -INT_MAX && b < INT_MAX)
      score = (a + b) / 2 - 1000;
    else
      score = board.nega_eval();
  } else
    score = board.nega_eval();
}

template <class Game>
inline bool move_filter(char piece, int,
                        const BoardImpl<Game>& board) noexcept {
  if (board.turn() < 8 && piece < 'j' /* size < 5 */)
    return false;
  else
    return true;
}

template <class Game>
class ChildCollector : public BoardImpl<Game>::MoveVisitor {
 public:
  ChildCollector(const BoardImpl<Game>& b, Hash<Game>* h) : board(b), hash(h) {}
  bool filter(char piece, int orientation,
              const BoardImpl<Game>& board) noexcept override {
    return move_filter(piece, orientation, board);
  }
  bool visit_move(Move m) override {
    children.push_back(Child<Game>(board, m, hash));
    return true;
  }
  const BoardImpl<Game>& board;
  Hash<Game>* hash;
  std::vector<Child<Game>> children;
};

template <class Game>
class AlphaBetaVisitor : public BoardImpl<Game>::MoveVisitor {
 public:
  AlphaBetaVisitor(const BoardImpl<Game>& n, int a, int b)
      : node(n), alpha(a), beta(b) {}
  bool filter(char piece, int orientation,
              const BoardImpl<Game>& board) noexcept override {
    return move_filter(piece, orientation, board);
  }
  bool visit_move(Move m) override {
    visited_nodes++;
    int v = -node.child(m).nega_eval();
    if (v > alpha) {
      alpha = v;
      if (alpha >= beta) return false;
    }
    return true;
  }

  const BoardImpl<Game>& node;
  int alpha;
  int beta;
};

inline double round_(double num) {
  if (num < 0.0)
    return ceil(num - 0.5);
  else
    return floor(num + 0.5);
}

template <class Game>
int negascout_rec(const BoardImpl<Game>& node, int depth, int alpha, int beta,
                  Move* best_move, Hash<Game>* hash, Hash<Game>* prev_hash,
                  int hash_depth) {
  assert(alpha <= beta);

  if (++visited_nodes >= check_point && enable_timeout) {
    if ((int)(expire_clock - clock()) < 0) {
      timed_out = true;
      return 0;
    }
    check_point += CHECKPOINT_INTERVAL;
  }

  if (depth <= 1) {
    AlphaBetaVisitor<Game> visitor(node, alpha, beta);
    if (node.visit_moves(&visitor))
      return visitor.alpha;
    else
      return visitor.beta;
  }

  std::pair<int, int>* hash_entry = nullptr;
  if (hash_depth > 0) {
    auto found = hash->insert(
        std::make_pair(node.key(), std::make_pair(-INT_MAX, INT_MAX)));
    hash_entry = &found.first->second;
    if (!found.second) {
      int ha = hash_entry->first;
      int hb = hash_entry->second;
      if (hb <= alpha) return hb;
      if (ha >= beta) return ha;
      if (ha == hb) return ha;
      alpha = std::max(alpha, ha);
      beta = std::min(beta, hb);
    }
  }

#ifdef USE_PROBCUT

  /* ProbCut */
  const ProbCut* pc = probcut_entry(node.turn(), depth);

  if (pc) {
    double thresh;
    if (node.turn() >= 15)
      thresh = 2.0;
    else
      thresh = 1.6;

    if (beta < INT_MAX) {
      int bound = (int)round_((thresh * pc->sigma + beta - pc->b) / pc->a);
      int r = negascout_rec(node, pc->depth, bound - 1, bound, nullptr, hash,
                            prev_hash, 0);
      if (timed_out) return 0;
      if (r >= bound) {
        if (hash_entry) hash_entry->first = std::max(hash_entry->first, beta);
        return beta;
      }
    }
    if (alpha > -INT_MAX) {
      int bound = (int)round_((-thresh * pc->sigma + alpha - pc->b) / pc->a);
      int r = negascout_rec(node, pc->depth, bound, bound + 1, nullptr, hash,
                            prev_hash, 0);
      if (timed_out) return 0;
      if (r <= bound) {
        if (hash_entry)
          hash_entry->second = std::min(hash_entry->second, alpha);
        return alpha;
      }
    }
  }

#endif  // USE_PROBCUT

  ChildCollector<Game> collector(node, prev_hash + 1);
  node.visit_moves(&collector);
  std::vector<Child<Game>> children = std::move(collector.children);
  std::sort(children.begin(), children.end());

  bool found_pv = false;
  int score_max = -INT_MAX;
  int a = alpha;

  for (auto& child : children) {
    int score;
    if (found_pv) {
      score = -negascout_rec(child.board, depth - 1, -a - 1, -a, nullptr,
                             hash + 1, prev_hash + 1, hash_depth - 1);
      if (timed_out) return 0;
      if (score > a && score < beta) {
        score = -negascout_rec(child.board, depth - 1, -beta, -score, nullptr,
                               hash + 1, prev_hash + 1, hash_depth - 1);
        if (timed_out) return 0;
      }
    } else {
      score = -negascout_rec(child.board, depth - 1, -beta, -a, nullptr,
                             hash + 1, prev_hash + 1, hash_depth - 1);
      if (timed_out) return 0;
    }

    if (score >= beta) {
      if (hash_entry) hash_entry->first = std::max(hash_entry->first, score);
      return score;
    }

    if (score > score_max) {
      if (score > a) a = score;
      if (score > alpha) {
        found_pv = true;
        if (best_move) *best_move = child.move;
      }
      score_max = score;
    }
  }
  if (hash_entry) {
    if (score_max > alpha)
      hash_entry->first = hash_entry->second = score_max;
    else
      hash_entry->second = std::min(hash_entry->second, score_max);
  }
  return score_max;
}

}  // namespace

template <class Game>
SearchResult negascout(const BoardImpl<Game>& node, int max_depth, int stop_ms,
                       int timeout_ms) {
  Move best_move = Move::invalid();
  int score;

  clock_t start = clock();
  expire_clock = start + timeout_ms * (CLOCKS_PER_SEC / 1000);
  check_point = visited_nodes + CHECKPOINT_INTERVAL;
  timed_out = false;
  enable_timeout = false;

#ifdef PROBSTAT
  score =
      negascout_rec(node, 1, -INT_MAX, INT_MAX, nullptr, nullptr, nullptr, 0);
  printf("1> ? ???? (%d)\n", score);
#endif

  std::unique_ptr<Hash<Game>[]> hash, prev_hash;
  prev_hash = std::make_unique<Hash<Game>[]>(max_depth);
  for (int i = 2; i <= max_depth; i++) {
    hash = std::make_unique<Hash<Game>[]>(max_depth);
    Move move;
    score = negascout_rec(node, i, -INT_MAX, INT_MAX, &move, hash.get(),
                          prev_hash.get(), 8);
    if (timed_out) break;
    double sec = (double)(clock() - start) / CLOCKS_PER_SEC;
#ifdef BLOKUSDUO_VERBOSE
    printf("%d> %.3f %s (%d)\n", i, sec, move.fourcc().c_str(), score);
#endif
    prev_hash = std::move(hash);
    best_move = move;
    enable_timeout = true;
    if (sec * 1000 > stop_ms) break;
  }

  return SearchResult(best_move, score);
}
template SearchResult negascout<BlokusDuoMini>(
    const BoardImpl<BlokusDuoMini>& node, int max_depth, int stop_ms,
    int timeout_ms);
template SearchResult negascout<BlokusDuoStandard>(
    const BoardImpl<BlokusDuoStandard>& node, int max_depth, int stop_ms,
    int timeout_ms);

template <class Game>
using WldHash = std::unordered_map<typename BoardImpl<Game>::Key, int,
                                   typename BoardImpl<Game>::Key::Hash>;

template <class Game>
int wld_rec(const BoardImpl<Game>& node, int alpha, int beta,
            WldHash<Game>* hash) {
  typename BoardImpl<Game>::Key key(node.key());
  auto i = hash->find(key);
  if (i != hash->end()) return node.is_violet_turn() ? i->second : -i->second;

  if (++visited_nodes >= check_point) {
    if ((int)(expire_clock - clock()) < 0) throw Timeout();
    check_point += CHECKPOINT_INTERVAL;
  }

  std::vector<Move> valid_moves = node.valid_moves();
  if (valid_moves[0].is_pass()) {
    int score = node.relative_score();
    if (score < 0)
      return score;
    else if (score == 0) {
      valid_moves = node.child(valid_moves[0]).valid_moves();
      if (valid_moves[0].is_pass())
        return 0;
      else
        return -block_set[valid_moves[0].piece_id()].size;
    }
  }

  for (Move move : valid_moves) {
    auto child = node.child(move);
    int v = -wld_rec(child, -beta, -alpha, hash + 1);
    if (v > alpha) {
      alpha = v;
      if (alpha > 0 || alpha >= beta) break;
    }
  }
  (*hash)[key] = node.is_violet_turn() ? alpha : -alpha;
  return alpha;
}

template <class Game>
SearchResult wld(const BoardImpl<Game>& node, int timeout_sec) {
  expire_clock = clock() + timeout_sec * CLOCKS_PER_SEC;
  check_point = visited_nodes + CHECKPOINT_INTERVAL;

  WldHash<Game> hash[42];
  visited_nodes++;

  int alpha = -INT_MAX, beta = INT_MAX;
  std::vector<Move> valid_moves = node.valid_moves();
  Move wld_move = Move::invalid();

  for (Move move : valid_moves) {
    BoardImpl<Game> child = node.child(move);
    int v = -wld_rec(child, -beta, -alpha, hash);
    if (v > alpha) {
      alpha = v;
      wld_move = move;
      if (alpha > 0 || alpha >= beta) break;
    }
  }
  return SearchResult(wld_move, alpha);
}
template SearchResult wld<BlokusDuoMini>(const BoardImpl<BlokusDuoMini>& node,
                                         int timeout_sec);
template SearchResult wld<BlokusDuoStandard>(
    const BoardImpl<BlokusDuoStandard>& node, int timeout_sec);

template <class Game>
int perfect_rec(const BoardImpl<Game>& node, int alpha, int beta,
                WldHash<Game>* hash) {
  typename BoardImpl<Game>::Key key(node.key());
  auto i = hash->find(key);
  if (i != hash->end()) return node.is_violet_turn() ? i->second : -i->second;

  visited_nodes++;

  for (Move move : node.valid_moves()) {
    BoardImpl<Game> child = node.child(move);
    if (child.is_game_over()) {
      assert(move.is_pass());
      return node.relative_score();
    }
    int v = -perfect_rec(child, -beta, -alpha, hash + 1);
    if (v > alpha) {
      alpha = v;
      if (alpha >= beta) {
        (*hash)[key] = node.is_violet_turn() ? beta : -beta;
        return beta;
      }
    }
  }
  (*hash)[key] = node.is_violet_turn() ? alpha : -alpha;
  return alpha;
}

template <class Game>
SearchResult perfect(const BoardImpl<Game>& node) {
  constexpr int max_turn = Game::NUM_PIECES * 2 + 2;
  auto hash = std::make_unique<WldHash<Game>[]>(max_turn - node.turn());

  visited_nodes++;

  int alpha = -INT_MAX, beta = INT_MAX;
  Move perfect_move = Move::invalid();
  for (Move move : node.valid_moves()) {
    auto child = node.child(move);
    int v = -perfect_rec(child, -beta, -alpha, hash.get());
    if (v > alpha) {
      alpha = v;
      perfect_move = move;
    }
  }
  return SearchResult(perfect_move, alpha);
}
template SearchResult perfect<BlokusDuoMini>(
    const BoardImpl<BlokusDuoMini>& node);
template SearchResult perfect<BlokusDuoStandard>(
    const BoardImpl<BlokusDuoStandard>& node);

template <class Game>
Move opening_move(const BoardImpl<Game>& b) {
  if (b.turn() == 0) {
    static const std::array<Move, 10> good_first_moves = {
        Move("56t2"), Move("65u0"), Move("66p4"), Move("56o4"), Move("56t6"),
        Move("65o6"), Move("66t0"), Move("64r2"), Move("55t2"), Move("75o2")};
    int i =
        (int)((rand() / ((double)RAND_MAX + 1.0f)) * good_first_moves.size());
    return good_first_moves[i];
  }
  return Move::invalid();
}
template Move opening_move<BlokusDuoMini>(const BoardImpl<BlokusDuoMini>& b);
template Move opening_move<BlokusDuoStandard>(
    const BoardImpl<BlokusDuoStandard>& b);

#if 0
void wld_test()
{
    const char *moves[] = {
        "56t2","9Ao2","39n2","6Dq0","69s2","B8u0","96l7","B5r0","84m3",
        "85m7","D7p3","44l7","43k7","17k4","C4r0","EAn0","1Co5","3Ej2",
        "12g0","72p4","99c2","A1i1","E1q3","3Bt0","CAu0", nullptr
    };
    Board b;
    for (int i = 0; moves[i]; i++)
        b.play_move(Move(moves[i]));

    b.show();

    try {
        for (int npass = 0; npass < 2;) {
            clock_t start = clock();
            visited_nodes = 0;

            SearchResult result = wld(&b, 5);
            Move m = result.first;

            double sec = (double)(clock() - start) / CLOCKS_PER_SEC;
            printf("time(%d): %d nodes / %.3f sec (%d nps)\n",
                   b.turn(), visited_nodes, sec, (int)(visited_nodes / sec));
            printf("\n%s (%d)\n", m.fourcc().c_str(), result.second);

            npass = m.is_pass() ? npass+1 : 0;
            b.play_move(m);
            b.show();
        }
    }
    catch (Timeout& e) {
        printf("timeout\n");
    }
}

void perfect_test()
{
    const char *moves[] = {
        "46k4","9Ao7","86o7","5Cn1","A8n1","CAt6","5Am3","77m6","7Bq2",
        "8Cq2","D9r0","3Dk1","2At2","CEl2","CCp3","B8a0","D6s0","D7b2",
        "A4u0","B6d1","C3l1","----","16j0","----","AEg6","----","81i0",
        "----", nullptr
    };
    Board b;
    for (int i = 0; moves[i]; i++)
        b.play_move(Move(moves[i]));

    b.show();

    for (int npass = 0; npass < 2;) {
        clock_t start = clock();
        visited_nodes = 0;

        SearchResult result = perfect(&b);
        Move m = result.first;

        double sec = (double)(clock() - start) / CLOCKS_PER_SEC;
        printf("time(%d): %d nodes / %.3f sec (%d nps)\n",
               b.turn(), visited_nodes, sec, (int)(visited_nodes / sec));
        printf("\n%s (%d)\n", m.fourcc().c_str(), result.second);

        npass = m.is_pass() ? npass+1 : 0;
        b.play_move(m);
        b.show();
    }
}

int main()
{
    wld_test();
    return 0;
}
#endif

}  // namespace blokusduo::search
