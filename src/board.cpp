#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <bit>

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
BoardImpl<Game>::BoardImpl() {
  at(Game::START1X, Game::START1Y) = VIOLET_CORNER;
  at(Game::START2X, Game::START2Y) = ORANGE_CORNER;
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

  for (int i = 0; i < piece->size; i++) {
    int x = px + piece->coords[i].x;
    int y = py + piece->coords[i].y;
    if (at(x, y) & (is_violet_turn() ? VIOLET_CORNER : ORANGE_CORNER))
      return true;
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

    uint8_t block = is_violet_turn() ? VIOLET_TILE : ORANGE_TILE;
    uint8_t edge_bit = is_violet_turn() ? VIOLET_EDGE : ORANGE_EDGE;
    uint8_t corner_bit = is_violet_turn() ? VIOLET_CORNER : ORANGE_CORNER;

    for (int i = 0; i < piece->size; i++) {
      int x = px + piece->coords[i].x;
      int y = py + piece->coords[i].y;
      at(x, y) |= block;
      key_.set(player_, x, y);
      if (in_bounds(x - 1, y)) at(x - 1, y) |= edge_bit;
      if (in_bounds(x, y - 1)) at(x, y - 1) |= edge_bit;
      if (in_bounds(x + 1, y)) at(x + 1, y) |= edge_bit;
      if (in_bounds(x, y + 1)) at(x, y + 1) |= edge_bit;
      if (in_bounds(x - 1, y - 1)) at(x - 1, y - 1) |= corner_bit;
      if (in_bounds(x + 1, y - 1)) at(x + 1, y - 1) |= corner_bit;
      if (in_bounds(x - 1, y + 1)) at(x - 1, y + 1) |= corner_bit;
      if (in_bounds(x + 1, y + 1)) at(x + 1, y + 1) |= corner_bit;
    }
  }
  turn_++;
  player_ = opponent();
  key_.flip_player();
}

template <class Game>
bool BoardImpl<Game>::placeable(int px, int py,
                                const Piece* piece) const noexcept {
  uint8_t mask = is_violet_turn() ? VIOLET_TILE | VIOLET_EDGE | ORANGE_TILE
                                  : ORANGE_TILE | ORANGE_EDGE | VIOLET_TILE;

  for (int i = 0; i < piece->size; i++) {
    int x = px + piece->coords[i].x;
    int y = py + piece->coords[i].y;
    if (at(x, y) & mask) return false;
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

  // Generate corner candidates and test placements one board row at a time.
  constexpr uint16_t ROW_MASK = (uint16_t{1} << XSIZE) - 1;
  uint16_t blocked_rows[YSIZE];
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

        bool placeable = true;
        const int piece_x = x + piece->minx;
        const int piece_y = y + piece->miny;
        const uint8_t* rows = piece_row_masks[piece->id];
        for (int row = 0; row <= piece->maxy - piece->miny; row++) {
          if (blocked_rows[piece_y + row] & (rows[row] << piece_x)) {
            placeable = false;
            break;
          }
        }
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
      if (at(x, y) & VIOLET_TILE)
        s += 'V';
      else if (at(x, y) & ORANGE_TILE)
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

template <>
int BoardImpl<BlokusDuoStandard>::eval_influence() const {
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
