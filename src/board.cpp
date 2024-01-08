#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <bit>

#include "blokusduo.h"
#include "piece.h"

namespace blokusduo {
namespace {

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

}  // namespace

Move::Move(std::string_view code) {
  if (code[0] == '-')
    m_ = 0xffff;
  else {
    int xy;
    sscanf(code.data(), "%2X", &xy);
    int piece_id = (tolower(code[2]) - 'a') << 3 | (code[3] - '0');
    m_ = (xy - 0x11) | piece_id << 8;
  }
}

std::string Move::code() const noexcept {
  char buf[5];
  if (is_pass())
    strcpy(buf, "----");
  else
    sprintf(buf, "%2X%c%d", (m_ + 0x11) & 0xff, piece(), orientation());
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

  DiagPoint diag_neighbors[100], *diag_point;
  {
    uint8_t corner_mask = is_violet_turn() ? VIOLET_MASK | ORANGE_TILE
                                           : ORANGE_MASK | VIOLET_TILE;
    uint8_t corner_bit = is_violet_turn() ? VIOLET_CORNER : ORANGE_CORNER;
    uint8_t edge_bit = is_violet_turn() ? VIOLET_EDGE : ORANGE_EDGE;

    diag_point = diag_neighbors;
    for (int ey = 0; ey < YSIZE; ey++) {
      for (int ex = 0; ex < XSIZE; ex++) {
        if ((at(ex, ey) & corner_mask) == corner_bit) {
          diag_point->x = ex;
          diag_point->y = ey;
          diag_point->orientation =
              (ey > 0 && at(ex, ey - 1) & edge_bit)
                  ? (ex > 0 && (at(ex - 1, ey) & edge_bit) ? 0 : 1)
                  : (ex > 0 && (at(ex - 1, ey) & edge_bit) ? 2 : 3);
          diag_point++;
        }
      }
    }
    diag_point->x = -1;
  }

  int nmove = 0;
  for (const Piece* piece : Game::piece_set) {
    if (!is_piece_available(player_, piece->block_id())) continue;
    if (!visitor->filter(piece->block_id() + 'a', piece->orientation(), *this))
      continue;
    short checked[YSIZE];
    memset(checked, 0, sizeof(checked));
    for (diag_point = diag_neighbors; diag_point->x >= 0; diag_point++) {
      for (int i = 0; i < piece->nr_corners[diag_point->orientation]; i++) {
        int x = diag_point->x - piece->corners[diag_point->orientation][i].x;
        int y = diag_point->y - piece->corners[diag_point->orientation][i].y;
        if (y + piece->miny < 0 || y + piece->maxy >= YSIZE ||
            x + piece->minx < 0 || x + piece->maxx >= XSIZE ||
            (checked[y] & 1 << x))
          continue;
        checked[y] |= 1 << x;
        if (placeable(x, y, piece)) {
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

template <class Game>
int BoardImpl<Game>::eval_pieces() const {
  constexpr int table[] = {
      2,  4,  6,  6,  10, 10, 10, 10, 10, 16, 16,
      16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  };
  int score = 0;

  for (int i = 0; i < NUM_PIECES; i++) {
    if (is_piece_available(0, i)) score -= table[i];
    if (is_piece_available(1, i)) score += table[i];
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
  uint8_t b[16 * 15];
  uint8_t *influences[2][YSIZE * XSIZE], **pinf, **pnew_inf;
  int score = 0;

  for (int x = 0; x <= XSIZE; x++) b[x] = VIOLET_TILE | ORANGE_TILE;
  for (int y = 0; y <= YSIZE; y++)
    b[y * 15 + XSIZE] = VIOLET_TILE | ORANGE_TILE;
  for (int x = 0; x <= XSIZE; x++) b[225 + x] = VIOLET_TILE | ORANGE_TILE;

  for (int player = 0; player < 2; player++) {
    const uint8_t mask[2] = {VIOLET_MASK | ORANGE_TILE,
                             ORANGE_MASK | VIOLET_TILE};
    const uint8_t corner[2] = {VIOLET_CORNER, ORANGE_CORNER};

    pinf = influences[0];
    for (int y = 0; y < YSIZE; y++) {
      for (int x = 0; x < XSIZE; x++) {
        b[(y + 1) * 15 + x] = at(x, y) & mask[player];
        if (b[(y + 1) * 15 + x] == corner[player]) {
          *pinf++ = &b[(y + 1) * 15 + x];
          score++;
        }
      }
    }
    *pinf = nullptr;

    pinf = influences[0];
    pnew_inf = influences[1];
    while (*pinf) {
      uint8_t* pos = *pinf++;
      if (pos[-15] == 0) {
        pos[-15] = 1;
        *pnew_inf++ = pos - 15;
        score++;
      }
      if (pos[-1] == 0) {
        pos[-1] = 1;
        *pnew_inf++ = pos - 1;
        score++;
      }
      if (pos[1] == 0) {
        pos[1] = 1;
        *pnew_inf++ = pos + 1;
        score++;
      }
      if (pos[15] == 0) {
        pos[15] = 1;
        *pnew_inf++ = pos + 15;
        score++;
      }
    }
    *pnew_inf = nullptr;

    pinf = influences[1];
    pnew_inf = influences[0];
    while (*pinf) {
      uint8_t* pos = *pinf++;
      if (pos[-15] == 0) {
        pos[-15] = 1;
        *pnew_inf++ = pos - 15;
        score++;
      }
      if (pos[-1] == 0) {
        pos[-1] = 1;
        *pnew_inf++ = pos - 1;
        score++;
      }
      if (pos[1] == 0) {
        pos[1] = 1;
        *pnew_inf++ = pos + 1;
        score++;
      }
      if (pos[15] == 0) {
        pos[15] = 1;
        *pnew_inf++ = pos + 15;
        score++;
      }
    }
    *pnew_inf = nullptr;

    pinf = influences[0];
    while (*pinf) {
      uint8_t* pos = *pinf++;
      if (pos[-15] == 0) {
        pos[-15] = 1;
        score++;
      }
      if (pos[-1] == 0) {
        pos[-1] = 1;
        score++;
      }
      if (pos[1] == 0) {
        pos[1] = 1;
        score++;
      }
      if (pos[15] == 0) {
        pos[15] = 1;
        score++;
      }
    }

    score = -score;
  }
  return score;
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
