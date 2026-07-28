#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <bit>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__wasm_simd128__)
#include <arm_neon.h>
#endif

#include "blokusduo.h"
#include "piece.h"

namespace blokusduo {
namespace {

constexpr int PIECE_EVAL_VALUES[] = {
    2,  4,  6,  6,  10, 10, 10, 10, 10, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};

struct DiagPoint {
  int x, y, orientation;
};

template <class Game>
class MoveCollector : public BoardImpl<Game>::MoveVisitor {
 public:
  bool visit_move(Move m) override {
    moves.push_back(m);
    return true;
  }
  std::vector<Move> moves;
};

constexpr uint64_t shu8x8(uint64_t bits) { return bits << 8; }
constexpr uint64_t shd8x8(uint64_t bits) { return bits >> 8; }

constexpr uint64_t shl8x8(uint64_t bits) {
  constexpr uint64_t mask =
      0b01111111'01111111'01111111'01111111'01111111'01111111'01111111'01111111;
  return (bits & mask) << 1;
}

constexpr uint64_t shr8x8(uint64_t bits) {
  constexpr uint64_t mask =
      0b01111111'01111111'01111111'01111111'01111111'01111111'01111111'01111111;
  return (bits >> 1) & mask;
}

constexpr uint64_t inflate8x8(uint64_t bits) {
  return bits | shu8x8(bits) | shd8x8(bits) | shl8x8(bits) | shr8x8(bits);
}

int hex_to_int(char c) {
  if (isdigit(c)) return c - '0';
  if (islower(c)) return c - 'a' + 10;
  if (isupper(c)) return c - 'A' + 10;
  return -1;
}

}  // namespace

Move::Move(std::string_view code) {
  if (code.size() != 4) {
    m_ = INVALID;
    return;
  }
  if (code == "----" || code == "0000") {
    m_ = PASS;
    return;
  }
  int x = hex_to_int(code[0]) - 1;
  int y = hex_to_int(code[1]) - 1;
  int p = tolower(code[2]) - 'a';
  int o = code[3] - '0';
  if (x < 0 || x >= BlokusDuoStandard::XSIZE || y < 0 ||
      y >= BlokusDuoStandard::YSIZE || p < 0 ||
      p >= BlokusDuoStandard::NUM_PIECES || o < 0 || o >= 8) {
    m_ = INVALID;
    return;
  }
  m_ = p << 11 | o << 8 | x << 4 | y;
}

std::string Move::code() const noexcept {
  char buf[5];
  if (is_pass())
    strcpy(buf, "0000");
  else
    sprintf(buf, "%2x%c%d", (m_ + 0x11) & 0xff, piece(), orientation());
  return std::string(buf);
}

Move Move::canonicalize() const noexcept {
  if (is_pass()) return Move::pass();
  auto& rot = block_set[piece_id()].rotations[orientation()];
  int new_x = x() + rot.offset_x;
  int new_y = y() + rot.offset_y;
  return Move(new_x, new_y, rot.piece->id);
}

template <class Game>
bool BoardImpl<Game>::is_valid_move(Move move) const {
  if (move.is_pass()) return true;

  if (!is_piece_available(player_, move.piece_id())) return false;

  auto& rot = block_set[move.piece_id()].rotations[move.orientation()];
  int px = move.x() + rot.offset_x;
  int py = move.y() + rot.offset_y;
  const Piece* piece = rot.piece;

  if (px + piece->minx < 0 || px + piece->maxx >= XSIZE ||
      py + piece->miny < 0 || py + piece->maxy >= YSIZE ||
      !placeable(px, py, piece))
    return false;

  constexpr uint16_t ROW_MASK = (uint16_t{1} << XSIZE) - 1;
  const int piece_x = px + piece->minx;
  const int piece_y = py + piece->miny;
  const uint8_t* rows = piece_row_masks[piece->id];
  const int start_x = is_violet_turn() ? Game::START1X : Game::START2X;
  const int start_y = is_violet_turn() ? Game::START1Y : Game::START2Y;
  for (int row = 0; row <= piece->maxy - piece->miny; row++) {
    const int y = piece_y + row;
    const uint16_t vertical =
        (y > 0 ? key_.a[player_][y - 1] & ROW_MASK : 0) |
        (y + 1 < YSIZE ? key_.a[player_][y + 1] & ROW_MASK : 0);
    uint16_t corners = ((vertical << 1) | (vertical >> 1)) & ROW_MASK;
    if (y == start_y) corners |= uint16_t{1} << start_x;
    if (corners & (static_cast<uint16_t>(rows[row]) << piece_x)) return true;
  }
  return false;
}

template <class Game>
void BoardImpl<Game>::play_move(Move move) {
  if (move.is_pass()) {
    pieces_[player_] |= PASSED;
    key_.set_pass(player_);
  } else {
    piece_eval_ += (player_ == 0 ? 1 : -1) * PIECE_EVAL_VALUES[move.piece_id()];
    pieces_[player_] |= 1 << move.piece_id();
    auto& rot = block_set[move.piece_id()].rotations[move.orientation()];
    int px = move.x() + rot.offset_x;
    int py = move.y() + rot.offset_y;
    const Piece* piece = rot.piece;

    const int piece_x = px + piece->minx;
    const int piece_y = py + piece->miny;
    const uint8_t* rows = piece_row_masks[piece->id];
    for (int row = 0; row <= piece->maxy - piece->miny; row++) {
      key_.a[player_][piece_y + row] |= rows[row] << piece_x;
    }
  }
  turn_++;
  player_ = opponent();
  key_.flip_player();
}

template <class Game>
bool BoardImpl<Game>::placeable(int px, int py,
                                const Piece* piece) const noexcept {
  constexpr uint16_t ROW_MASK = (uint16_t{1} << XSIZE) - 1;
  const int piece_x = px + piece->minx;
  const int piece_y = py + piece->miny;
  const uint8_t* rows = piece_row_masks[piece->id];
  for (int row = 0; row <= piece->maxy - piece->miny; row++) {
    const int y = piece_y + row;
    const uint16_t own = key_.a[player_][y] & ROW_MASK;
    const uint16_t vertical =
        (y > 0 ? key_.a[player_][y - 1] & ROW_MASK : 0) |
        (y + 1 < YSIZE ? key_.a[player_][y + 1] & ROW_MASK : 0);
    const uint16_t edge =
        ((own << 1) | (own >> 1) | vertical) & ROW_MASK;
    const uint16_t blocked =
        own | edge | (key_.a[opponent()][y] & ROW_MASK);
    if (blocked & (static_cast<uint16_t>(rows[row]) << piece_x)) return false;
  }
  return true;
}

template <class Game>
std::vector<Move> BoardImpl<Game>::valid_moves() const {
  MoveCollector<Game> collector;
  visit_moves(&collector);
  return std::move(collector.moves);
}

template <class Game>
bool BoardImpl<Game>::visit_moves(MoveVisitor* visitor) const {
  if (turn() < 2) {
    const int startx = is_violet_turn() ? Game::START1X : Game::START2X;
    const int starty = is_violet_turn() ? Game::START1Y : Game::START2Y;
    for (const Piece* p : Game::piece_set) {
      if (!visitor->filter(p->block_id() + 'a', p->orientation(), *this))
        continue;
      for (int i = 0; i < p->size; i++) {
        int x = startx - p->coords[i].x;
        int y = starty - p->coords[i].y;
        if (x + p->minx >= 0 && y + p->miny >= 0 && x + p->maxx < XSIZE &&
            y + p->maxy < YSIZE) {
          // In blokusduo mini, the first move can block the opponent's first
          // move.
          if (Game::YSIZE <= BlokusDuoMini::YSIZE && turn() == 1 &&
              !placeable(x, y, p))
            continue;
          if (!visitor->visit_move(Move(x, y, p->id))) return false;
        }
      }
    }
    return true;
  }

  // Generate corner candidates and test placements with packed board rows.
  constexpr uint16_t ROW_MASK = (uint16_t{1} << XSIZE) - 1;
  // Padding keeps four-row loads within the array at the bottom edge.
  uint16_t blocked_rows[YSIZE + 3] = {};
  uint16_t edge_rows[YSIZE];
  uint16_t own_rows[YSIZE];
  for (int y = 0; y < YSIZE; y++) {
    own_rows[y] = key_.a[player_][y] & ROW_MASK;
  }
  for (int y = 0; y < YSIZE; y++) {
    const uint16_t vertical =
        (y > 0 ? own_rows[y - 1] : 0) |
        (y + 1 < YSIZE ? own_rows[y + 1] : 0);
    edge_rows[y] =
        ((own_rows[y] << 1) | (own_rows[y] >> 1) | vertical) & ROW_MASK;
    blocked_rows[y] =
        own_rows[y] | edge_rows[y] | (key_.a[opponent()][y] & ROW_MASK);
  }

  DiagPoint diag_neighbors[100], *diag_point = diag_neighbors;
  for (int y = 0; y < YSIZE; y++) {
    const uint16_t vertical =
        (y > 0 ? own_rows[y - 1] : 0) |
        (y + 1 < YSIZE ? own_rows[y + 1] : 0);
    uint16_t corners =
        ((vertical << 1) | (vertical >> 1)) & ~blocked_rows[y] & ROW_MASK;
    while (corners != 0) {
      const int x = std::countr_zero(corners);
      const uint16_t point = uint16_t{1} << x;
      const bool top_edge = y > 0 && (edge_rows[y - 1] & point);
      const bool left_edge = x > 0 && (edge_rows[y] & (point >> 1));
      diag_point->x = x;
      diag_point->y = y;
      diag_point->orientation =
          top_edge ? (left_edge ? 0 : 1) : (left_edge ? 2 : 3);
      diag_point++;
      corners &= corners - 1;
    }
  }
  diag_point->x = -1;

  int nmove = 0;
  for (const Piece* piece : Game::piece_set) {
    if (!is_piece_available(player_, piece->block_id())) continue;
    if (!visitor->filter(piece->block_id() + 'a', piece->orientation(), *this))
      continue;
    const int min_x = -piece->minx;
    const int max_x = XSIZE - 1 - piece->maxx;
    const int min_y = -piece->miny;
    const int max_y = YSIZE - 1 - piece->maxy;
    uint16_t checked[YSIZE] = {};
    const uint8_t* rows = piece_row_masks[piece->id];
    const uint64_t first_four_rows =
        uint64_t{rows[0]} | (uint64_t{rows[1]} << 16) |
        (uint64_t{rows[2]} << 32) | (uint64_t{rows[3]} << 48);
    for (diag_point = diag_neighbors; diag_point->x >= 0; diag_point++) {
      const int orientation = diag_point->orientation;
      for (int i = 0; i < piece->nr_corners[orientation]; i++) {
        const int x = diag_point->x - piece->corners[orientation][i].x;
        const int y = diag_point->y - piece->corners[orientation][i].y;
        if (static_cast<unsigned>(x - min_x) >
                static_cast<unsigned>(max_x - min_x) ||
            static_cast<unsigned>(y - min_y) >
                static_cast<unsigned>(max_y - min_y) ||
            (checked[y] & (uint16_t{1} << x)))
          continue;
        checked[y] |= uint16_t{1} << x;

        const int piece_x = x + piece->minx;
        const int piece_y = y + piece->miny;
        // Four 16-bit lanes cover every piece except the five-high bar.
        uint64_t first_four_blocked;
        memcpy(&first_four_blocked, blocked_rows + piece_y,
               sizeof(first_four_blocked));
        const bool placeable =
            (first_four_blocked & (first_four_rows << piece_x)) == 0 &&
            (rows[4] == 0 ||
             (blocked_rows[piece_y + 4] & (rows[4] << piece_x)) == 0);
        if (placeable) {
          if (!visitor->visit_move(Move(x, y, piece->id))) return false;
          nmove++;
        }
      }
    }
  }
  if (nmove == 0) return visitor->visit_move(Move::pass());

  return true;
}

template <class Game>
std::string BoardImpl<Game>::to_string() const {
  std::string s;
  for (int y = 0; y < YSIZE; y++) {
    for (int x = 0; x < XSIZE; x++) {
      if (has_tile(0, x, y))
        s += 'V';
      else if (has_tile(1, x, y))
        s += 'O';
      else
        s += '.';
    }
    s += '\n';
  }
  return s;
}

template <class Game>
int BoardImpl<Game>::score(int player) const noexcept {
  int score = 0;

  for (int i = 0; i < NUM_PIECES; i++) {
    if (!is_piece_available(player, i)) score += block_set[i].size;
  }
  return score;
}

template <>
int BoardImpl<BlokusDuoMini>::eval_influence() const {
  uint64_t vtile = *reinterpret_cast<const uint64_t*>(key_.a[0]);
  uint64_t otile = *reinterpret_cast<const uint64_t*>(key_.a[1]);
  uint64_t vmask = ~(inflate8x8(vtile) | otile);
  uint64_t omask = ~(inflate8x8(otile) | vtile);
  uint64_t vinfl = (shu8x8(shl8x8(vtile)) | shd8x8(shl8x8(vtile)) |
                    shu8x8(shr8x8(vtile)) | shd8x8(shr8x8(vtile))) &
                   vmask;
  uint64_t oinfl = (shu8x8(shl8x8(otile)) | shd8x8(shl8x8(otile)) |
                    shu8x8(shr8x8(otile)) | shd8x8(shr8x8(otile))) &
                   omask;
  vinfl = inflate8x8(vinfl) & vmask;
  vinfl = inflate8x8(vinfl) & vmask;
  oinfl = inflate8x8(oinfl) & omask;
  oinfl = inflate8x8(oinfl) & omask;
  return std::popcount(vinfl) - std::popcount(oinfl);
}

// Estimates each player's influence over open cells. For each player, first
// exclude occupied cells and cells sharing an edge with one of their tiles,
// then seed every unblocked diagonal neighbor (or the starting point before
// the first move). Influence consists of those seeds and all cells reachable
// from them through unblocked orthogonal moves in at most three steps. The
// returned value is violet's count minus orange's count.
//
// Every implementation below follows that algorithm with a different packed
// board representation. The neighbor helpers perform bit-parallel shifts in
// the x and y directions; the main loop builds the blocked set and runs the
// three expansion steps.
template <>
int BoardImpl<BlokusDuoStandard>::eval_influence() const {
#if defined(__AVX2__)
  // Pack all 14 rows into one 256-bit register. Each 64-bit lane holds four
  // 16-bit row slots: 14 board bits followed by two zero padding bits. Shifts
  // by one move horizontally, shifts by 16 move vertically within a lane, and
  // 64-bit lane permutations carry rows across lane boundaries. The last
  // 64-bit lane contains only rows 12 and 13.
  constexpr uint64_t FOUR_ROWS = 0x3fff3fff3fff3fff;
  const __m256i board_mask = _mm256_set_epi64x(
      0x000000003fff3fff, FOUR_ROWS, FOUR_ROWS, FOUR_ROWS);
  const __m256i no_low_word =
      _mm256_set_epi64x(-1, -1, -1, 0);
  const __m256i no_high_word =
      _mm256_set_epi64x(0, -1, -1, -1);

  const auto vertical_neighbors = [&](const __m256i bits) {
    const __m256i previous_words = _mm256_and_si256(
        _mm256_permute4x64_epi64(bits, _MM_SHUFFLE(2, 1, 0, 0)),
        no_low_word);
    const __m256i next_words = _mm256_and_si256(
        _mm256_permute4x64_epi64(bits, _MM_SHUFFLE(3, 3, 2, 1)),
        no_high_word);
    return _mm256_or_si256(
        _mm256_or_si256(_mm256_srli_epi64(bits, 16),
                        _mm256_slli_epi64(bits, 16)),
        _mm256_or_si256(_mm256_srli_epi64(previous_words, 48),
                        _mm256_slli_epi64(next_words, 48)));
  };
  const auto orthogonal_neighbors = [&](const __m256i bits) {
    return _mm256_and_si256(
        _mm256_or_si256(
            vertical_neighbors(bits),
            _mm256_or_si256(_mm256_slli_epi64(bits, 1),
                            _mm256_srli_epi64(bits, 1))),
        board_mask);
  };
  const auto diagonal_neighbors = [&](const __m256i bits) {
    const __m256i vertical = vertical_neighbors(bits);
    return _mm256_and_si256(
        _mm256_or_si256(_mm256_slli_epi64(vertical, 1),
                        _mm256_srli_epi64(vertical, 1)),
        board_mask);
  };

  __m256i tiles[2];
  for (int player = 0; player < 2; player++) {
    // Loading rows 6-13 lets the shift zero-pad the upper lane.
    const __m128i first_eight = _mm_loadu_si128(
        reinterpret_cast<const __m128i*>(key_.a[player]));
    const __m128i last_eight = _mm_loadu_si128(
        reinterpret_cast<const __m128i*>(key_.a[player] + 6));
    const __m128i last_six = _mm_srli_si128(last_eight, 4);
    tiles[player] = _mm256_and_si256(
        _mm256_inserti128_si256(_mm256_castsi128_si256(first_eight),
                                last_six, 1),
        board_mask);
  }

  int influence[2] = {};
  for (int player = 0; player < 2; player++) {
    const __m256i edge = orthogonal_neighbors(tiles[player]);
    __m256i corner = diagonal_neighbors(tiles[player]);
    if (_mm256_testz_si256(tiles[player], tiles[player])) {
      const __m256i start =
          player == 0
              ? _mm256_set_epi64x(0, 0, uint64_t{1} << 4, 0)
              : _mm256_set_epi64x(0, uint64_t{1} << 25, 0, 0);
      corner = _mm256_or_si256(corner, start);
    }

    const __m256i blocked_without_corner = _mm256_or_si256(
        _mm256_or_si256(tiles[player], edge), tiles[1 - player]);
    const __m256i blocked =
        _mm256_or_si256(blocked_without_corner, corner);
    __m256i frontier =
        _mm256_andnot_si256(blocked_without_corner, corner);
    __m256i reached = frontier;
    const __m256i traversable = _mm256_andnot_si256(blocked, board_mask);

    for (int distance = 0; distance < 3; distance++) {
      const __m256i adjacent = orthogonal_neighbors(frontier);
      frontier = _mm256_andnot_si256(
          reached, _mm256_and_si256(adjacent, traversable));
      reached = _mm256_or_si256(reached, frontier);
    }

#if defined(__AVX512VPOPCNTDQ__) && defined(__AVX512VL__)
    // Use vector popcount when the AVX-512 extension is available for 256-bit
    // registers; otherwise store the four words and count them scalarly.
    const __m256i counts = _mm256_popcnt_epi64(reached);
    const __m128i pair_sums =
        _mm_add_epi64(_mm256_castsi256_si128(counts),
                      _mm256_extracti128_si256(counts, 1));
    influence[player] =
        _mm_cvtsi128_si64(pair_sums) + _mm_extract_epi64(pair_sums, 1);
#else
    alignas(32) uint64_t words[4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(words), reached);
    for (uint64_t word : words) influence[player] += std::popcount(word);
#endif
  }
  return influence[0] - influence[1];
#elif defined(__ARM_NEON) || defined(__wasm_simd128__)
  // Each 16-bit lane represents one 14-bit board row, so rows 0-7 occupy
  // `low` and rows 8-13 occupy the first six lanes of `high`; its last two
  // lanes are masked out. Per-lane bit shifts move horizontally, while vextq
  // shifts whole rows and carries them between the two registers. Emscripten
  // maps these 128-bit NEON intrinsics to WebAssembly SIMD.
  struct SimdRows {
    uint16x8_t low;
    uint16x8_t high;
  };

  const uint16x8_t zeros = vdupq_n_u16(0);
  uint16x8_t high_mask = vdupq_n_u16(0x3fff);
  high_mask = vsetq_lane_u16(0, high_mask, 6);
  high_mask = vsetq_lane_u16(0, high_mask, 7);
  const SimdRows board_mask = {
      vdupq_n_u16(0x3fff),
      high_mask};

  const auto vertical_neighbors = [zeros](const SimdRows& rows) {
    const uint16x8_t previous_low = vextq_u16(zeros, rows.low, 7);
    const uint16x8_t next_low = vextq_u16(rows.low, rows.high, 1);
    const uint16x8_t previous_high = vextq_u16(rows.low, rows.high, 7);
    const uint16x8_t next_high = vextq_u16(rows.high, zeros, 1);
    return SimdRows{vorrq_u16(previous_low, next_low),
                    vorrq_u16(previous_high, next_high)};
  };
  const auto orthogonal_neighbors =
      [&board_mask, &vertical_neighbors](const SimdRows& rows) {
        const SimdRows vertical = vertical_neighbors(rows);
        return SimdRows{
            vandq_u16(
                vorrq_u16(vertical.low,
                          vorrq_u16(vshlq_n_u16(rows.low, 1),
                                    vshrq_n_u16(rows.low, 1))),
                board_mask.low),
            vandq_u16(
                vorrq_u16(vertical.high,
                          vorrq_u16(vshlq_n_u16(rows.high, 1),
                                    vshrq_n_u16(rows.high, 1))),
                board_mask.high)};
      };
  const auto diagonal_neighbors =
      [&board_mask, &vertical_neighbors](const SimdRows& rows) {
        const SimdRows vertical = vertical_neighbors(rows);
        return SimdRows{
            vandq_u16(
                vorrq_u16(vshlq_n_u16(vertical.low, 1),
                          vshrq_n_u16(vertical.low, 1)),
                board_mask.low),
            vandq_u16(
                vorrq_u16(vshlq_n_u16(vertical.high, 1),
                          vshrq_n_u16(vertical.high, 1)),
                board_mask.high)};
      };

  SimdRows tiles[2];
  for (int player = 0; player < 2; player++) {
    const uint16x8_t first_eight = vld1q_u16(key_.a[player]);
    // Start at row 6 so the load stays within the 14-row array, then discard
    // its first two rows and shift zeros into the unused positions.
    const uint16x8_t last_eight = vld1q_u16(key_.a[player] + 6);
    const uint16x8_t last_six = vextq_u16(last_eight, zeros, 2);
    tiles[player] = {
        vandq_u16(first_eight, board_mask.low),
        vandq_u16(last_six, board_mask.high)};
  }

  int influence[2] = {};
  for (int player = 0; player < 2; player++) {
    const SimdRows edge = orthogonal_neighbors(tiles[player]);
    SimdRows corner = diagonal_neighbors(tiles[player]);
    const uint64x2_t tile_words =
        vreinterpretq_u64_u16(vorrq_u16(tiles[player].low,
                                        tiles[player].high));
    if ((vgetq_lane_u64(tile_words, 0) |
         vgetq_lane_u64(tile_words, 1)) == 0) {
      if (player == 0) {
        corner.low = vsetq_lane_u16(
            vgetq_lane_u16(corner.low, 4) | (uint16_t{1} << 4),
            corner.low, 4);
      } else {
        corner.high = vsetq_lane_u16(
            vgetq_lane_u16(corner.high, 1) | (uint16_t{1} << 9),
            corner.high, 1);
      }
    }

    const SimdRows blocked_without_corner = {
        vorrq_u16(vorrq_u16(tiles[player].low, edge.low),
                  tiles[1 - player].low),
        vorrq_u16(vorrq_u16(tiles[player].high, edge.high),
                  tiles[1 - player].high)};
    const SimdRows blocked = {
        vorrq_u16(blocked_without_corner.low, corner.low),
        vorrq_u16(blocked_without_corner.high, corner.high)};
    SimdRows frontier = {
        vbicq_u16(corner.low, blocked_without_corner.low),
        vbicq_u16(corner.high, blocked_without_corner.high)};
    SimdRows reached = frontier;
    const SimdRows traversable = {
        vbicq_u16(board_mask.low, blocked.low),
        vbicq_u16(board_mask.high, blocked.high)};

    for (int distance = 0; distance < 3; distance++) {
      const SimdRows adjacent = orthogonal_neighbors(frontier);
      frontier = {
          vbicq_u16(vandq_u16(adjacent.low, traversable.low), reached.low),
          vbicq_u16(vandq_u16(adjacent.high, traversable.high), reached.high)};
      reached = {vorrq_u16(reached.low, frontier.low),
                 vorrq_u16(reached.high, frontier.high)};
    }

    // Emscripten's NEON compatibility implementation scalarizes vcntq_u8.
    // Clang's elementwise popcount builtin lowers to the same native NEON
    // instruction and directly to i8x16.popcnt in WebAssembly.
#if defined(__clang__)
    const uint8x16_t byte_counts = vaddq_u8(
        __builtin_elementwise_popcount(
            vreinterpretq_u8_u16(reached.low)),
        __builtin_elementwise_popcount(
            vreinterpretq_u8_u16(reached.high)));
#else
    const uint8x16_t byte_counts = vaddq_u8(
        vcntq_u8(vreinterpretq_u8_u16(reached.low)),
        vcntq_u8(vreinterpretq_u8_u16(reached.high)));
#endif
    const uint16x8_t pair_counts = vpaddlq_u8(byte_counts);
    const uint32x4_t quad_counts = vpaddlq_u16(pair_counts);
    const uint64x2_t half_counts = vpaddlq_u32(quad_counts);
    influence[player] = vgetq_lane_u64(half_counts, 0) +
                        vgetq_lane_u64(half_counts, 1);
  }
  return influence[0] - influence[1];
#else
  // Use the same four-word packing as the AVX2 branch, but operate on scalar
  // 64-bit values. Each word holds four 16-bit row slots with two padding bits
  // per row; explicit neighboring-word terms carry vertical shifts across
  // word boundaries.
  using Bits = std::array<uint64_t, 4>;
  constexpr uint64_t FOUR_ROWS = 0x3fff3fff3fff3fff;
  constexpr Bits BOARD_MASK = {
      FOUR_ROWS, FOUR_ROWS, FOUR_ROWS, 0x000000003fff3fff};

  const auto orthogonal_neighbors = [&BOARD_MASK](const Bits& bits) {
    Bits result;
    for (int word = 0; word < 4; word++) {
      const uint64_t vertical =
          (bits[word] >> 16) | (bits[word] << 16) |
          (word > 0 ? bits[word - 1] >> 48 : 0) |
          (word < 3 ? bits[word + 1] << 48 : 0);
      result[word] =
          ((bits[word] << 1) | (bits[word] >> 1) | vertical) &
          BOARD_MASK[word];
    }
    return result;
  };
  const auto diagonal_neighbors = [&BOARD_MASK](const Bits& bits) {
    Bits result;
    for (int word = 0; word < 4; word++) {
      const uint64_t vertical =
          (bits[word] >> 16) | (bits[word] << 16) |
          (word > 0 ? bits[word - 1] >> 48 : 0) |
          (word < 3 ? bits[word + 1] << 48 : 0);
      result[word] = ((vertical << 1) | (vertical >> 1)) & BOARD_MASK[word];
    }
    return result;
  };

  Bits tiles[2] = {};
  for (int player = 0; player < 2; player++) {
    for (int y = 0; y < YSIZE; y++) {
      tiles[player][y / 4] |=
          static_cast<uint64_t>(key_.a[player][y] & 0x3fff) << (y % 4 * 16);
    }
  }

  int influence[2] = {};
  for (int player = 0; player < 2; player++) {
    Bits edge = orthogonal_neighbors(tiles[player]);
    Bits corner = diagonal_neighbors(tiles[player]);
    bool has_tiles = false;
    for (uint64_t word : tiles[player]) has_tiles |= word != 0;
    if (!has_tiles) {
      const int x = player == 0 ? BlokusDuoStandard::START1X
                                : BlokusDuoStandard::START2X;
      const int y = player == 0 ? BlokusDuoStandard::START1Y
                                : BlokusDuoStandard::START2Y;
      corner[y / 4] |= uint64_t{1} << (y % 4 * 16 + x);
    }

    Bits traversable;
    Bits reached;
    Bits frontier;
    for (int word = 0; word < 4; word++) {
      const uint64_t blocked =
          tiles[player][word] | edge[word] | corner[word] |
          tiles[1 - player][word];
      frontier[word] =
          corner[word] &
          ~(tiles[player][word] | edge[word] | tiles[1 - player][word]);
      reached[word] = frontier[word];
      traversable[word] = ~blocked & BOARD_MASK[word];
    }

    for (int distance = 0; distance < 3; distance++) {
      const Bits adjacent = orthogonal_neighbors(frontier);
      for (int word = 0; word < 4; word++) {
        frontier[word] = adjacent[word] & traversable[word] & ~reached[word];
        reached[word] |= frontier[word];
      }
    }
    for (uint64_t word : reached) influence[player] += std::popcount(word);
  }
  return influence[0] - influence[1];
#endif
}

// static
template <class Game>
std::vector<Move> BoardImpl<Game>::all_possible_moves() {
  std::vector<Move> moves;
  for (const Piece* p : Game::piece_set) {
    for (int y = 0; y < YSIZE; y++) {
      for (int x = 0; x < XSIZE; x++) {
        if (x + p->minx >= 0 && y + p->miny >= 0 && x + p->maxx < XSIZE &&
            y + p->maxy < YSIZE) {
          moves.push_back(Move(x, y, p->id));
        }
      }
    }
  }
  moves.push_back(Move::pass());
  return moves;
}

// static
template <class Game>
Move BoardImpl<Game>::rotate_move(Move m, int rotation) {
  if (m.is_pass()) return m;
  m = m.canonicalize();
  int x, y;
  switch (rotation & 7) {
    case 0:
      x = m.x();
      y = m.y();
      break;
    case 1:
      x = XSIZE - 1 - m.x();
      y = m.y();
      break;
    case 2:
      x = XSIZE - 1 - m.y();
      y = m.x();
      break;
    case 3:
      x = m.y();
      y = m.x();
      break;
    case 4:
      x = XSIZE - 1 - m.x();
      y = YSIZE - 1 - m.y();
      break;
    case 5:
      x = m.x();
      y = YSIZE - 1 - m.y();
      break;
    case 6:
      x = m.y();
      y = YSIZE - 1 - m.x();
      break;
    case 7:
      x = XSIZE - 1 - m.y();
      y = YSIZE - 1 - m.x();
      break;
  }
  int orientation =
      (m.orientation() + (m.orientation() & 1 ? 8 - rotation : rotation)) & 7;
  return Move(x, y, m.piece_id() << 3 | orientation).canonicalize();
}

// explicit instantiation
template class BoardImpl<BlokusDuoMini>;
template class BoardImpl<BlokusDuoStandard>;

}  // namespace blokusduo
