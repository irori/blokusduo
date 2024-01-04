#ifndef BLOKUSDUO_H_
#define BLOKUSDUO_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blokusduo {

class Piece;

// Represents a move in the game of Blokus Duo.
class Move {
 public:
  Move() : m_(INVALID) {}
  Move(std::string_view fourcc);
  Move(int x, int y, int piece_id) : m_(x << 4 | y | piece_id << 8) {}
  int x() const { return m_ >> 4 & 0xf; }
  int y() const { return m_ & 0xf; }
  char piece() const { return 'a' + piece_id(); }
  int piece_id() const { return m_ >> 11; }
  int orientation() const { return m_ >> 8 & 0x7; }
  bool is_pass() const { return m_ == PASS; }
  bool is_valid() const { return m_ != INVALID; }
  std::string fourcc() const;
  Move canonicalize() const;
  bool operator<(const Move& rhs) const { return m_ < rhs.m_; }
  bool operator==(const Move& rhs) const { return m_ == rhs.m_; }
  bool operator!=(const Move& rhs) const { return m_ != rhs.m_; }
  Move mirror() const;

  struct Hash {
    size_t operator()(Move key) const { return std::hash<uint16_t>{}(key.m_); }
  };

  static Move pass() { return Move(PASS); }
  static Move invalid() { return Move(); }

 private:
  constexpr static uint16_t PASS = 0xffff;
  constexpr static uint16_t INVALID = 0xfffe;
  Move(uint16_t m) : m_(m){};

  uint16_t m_;
};

// This class encapsulates configurations specific to the standard version of
// Blokus Duo, such as board size and piece types. Designed to be used as a
// template parameter, allowing the game mechanics to be easily swapped in and
// out for different versions of the game.
class BlokusDuoStandard {
 public:
  constexpr static int NUM_PIECES = 21;
  constexpr static int NUM_ORIENTED_PIECES = 91;
  constexpr static int XSIZE = 14;
  constexpr static int YSIZE = 14;
  constexpr static int START1X = 4;
  constexpr static int START1Y = 4;
  constexpr static int START2X = 9;
  constexpr static int START2Y = 9;

  // Compact board representation suitable for hash key.
  struct Key {
    struct Hash {
      size_t operator()(const Key& key) const noexcept {
        return std::hash<std::string_view>{}(key.string_view());
      }
    };

    bool operator==(const Key& rhs) const noexcept {
      return memcmp(this, &rhs, sizeof(Key)) == 0;
    }
    bool operator!=(const Key& rhs) const noexcept { return !(*this == rhs); }
    std::string_view string_view() const noexcept {
      return std::string_view(reinterpret_cast<const char*>(this), sizeof(Key));
    }
    void set(int player, int x, int y) { a[player][y] |= 1 << x; }
    void set_pass(int player) { a[player][0] |= 1 << XSIZE; }
    void flip_player() { a[0][1] ^= 1 << XSIZE; }
    uint16_t a[2][YSIZE] = {};
  };

  static const std::array<const Piece*, NUM_ORIENTED_PIECES> piece_set;
};

// Represents a simplified version of Blokus Duo, with a smaller board (8x8)
// and fewer pieces (no pentominoes).
class BlokusDuoMini {
 public:
  constexpr static int NUM_PIECES = 9;
  constexpr static int NUM_ORIENTED_PIECES = 28;
  constexpr static int XSIZE = 8;
  constexpr static int YSIZE = 8;
  constexpr static int START1X = 2;
  constexpr static int START1Y = 2;
  constexpr static int START2X = 5;
  constexpr static int START2Y = 5;

  // Compact board representation suitable for hash key.
  struct Key {
    struct Hash {
      size_t operator()(const Key& key) const noexcept {
        return std::hash<std::string_view>{}(key.string_view());
      }
    };

    bool operator==(const Key& rhs) const noexcept {
      return memcmp(this, &rhs, sizeof(Key)) == 0;
    }
    bool operator!=(const Key& rhs) const noexcept { return !(*this == rhs); }
    std::string_view string_view() const noexcept {
      return std::string_view(reinterpret_cast<const char*>(this), sizeof(Key));
    }
    void set(int player, int x, int y) { a[player][y] |= 1 << x; }
    void set_pass(int player) { flags |= 1 << player; }
    void flip_player() { flags ^= 4; }
    uint8_t a[2][YSIZE] = {};
    uint8_t flags = 0;
  };

  static const std::array<const Piece*, NUM_ORIENTED_PIECES> piece_set;
};

// This class encapsulates the state of the game board. It provides methods for
// making moves, enumerating valid moves, and calculating the score.
template <class Game>
class BoardImpl {
 public:
  constexpr static int NUM_PIECES = Game::NUM_PIECES;
  constexpr static int XSIZE = Game::XSIZE;
  constexpr static int YSIZE = Game::YSIZE;

  BoardImpl();

  // Accessors.
  int player() const { return player_; }
  int opponent() const { return 1 - player_; }
  int turn() const { return turn_; }
  bool is_game_over() const { return pieces_[0] & pieces_[1] & PASSED; }
  bool is_violet_turn() const { return player_ == 0; }
  bool is_valid_move(Move move) const;
  bool is_piece_available(int player, int piece) const {
    return (pieces_[player] & (1 << piece)) == 0;
  }
  bool did_pass(int player) const { return pieces_[player] & PASSED; }
  using Key = typename Game::Key;
  const Key& key() const { return key_; }

  // Accessing the board. The board is represented as a 2D array of cells, with
  // each cell containing a bit mask of the following flags:
  constexpr static uint8_t VIOLET_TILE = 0x01;  // Occupied by violet piece.
  constexpr static uint8_t ORANGE_TILE = 0x02;  // Occupied by orange piece.
  constexpr static uint8_t VIOLET_EDGE = 0x04;  // Adjacent to violet piece.
  constexpr static uint8_t ORANGE_EDGE = 0x08;  // Adjacent to orange piece.
  constexpr static uint8_t VIOLET_CORNER =
      0x10;  // Diagonally adjacent to violet piece.
  constexpr static uint8_t ORANGE_CORNER =
      0x20;  // Diagonally adjacent to orange piece.
  constexpr static uint8_t VIOLET_MASK =
      VIOLET_TILE | VIOLET_EDGE | VIOLET_CORNER;
  constexpr static uint8_t ORANGE_MASK =
      ORANGE_TILE | ORANGE_EDGE | ORANGE_CORNER;

  uint8_t& at(int x, int y) { return cells[y][x]; }
  const uint8_t& at(int x, int y) const { return cells[y][x]; }
  const uint8_t (&data() const)[YSIZE][XSIZE] { return cells; }

  // Enumerating moves.
  class MoveVisitor {
   public:
    virtual ~MoveVisitor() = default;

    // Return false to skip moves for the piece. This can be used to remove
    // small pieces from consideration in the early stage, for performance.
    virtual bool filter([[maybe_unused]] char piece,
                        [[maybe_unused]] int orientation,
                        [[maybe_unused]] const BoardImpl& board) noexcept {
      return true;
    }

    // Return false to stop visiting moves.
    virtual bool visit_move(Move m) = 0;
  };

  // Returns false if the visitor stopped visiting moves.
  bool visit_moves(MoveVisitor* visitor) const;

  // A shortcut for visit_moves() that returns a vector of moves.
  std::vector<Move> valid_moves() const;

  // Plays a move, modifying the board state.
  void play_move(Move move);

  // Returns a copy of the board with the move applied.
  BoardImpl child(Move move) const {
    BoardImpl c(*this);
    c.play_move(move);
    return c;
  }

  // Returns a textual representation of the board.
  std::string to_string() const;

  // Returns the score (number of tiles placed on the board) for the given
  // player.
  int score(int player) const noexcept;

  // Returns the score for the current player, with the opponent's score
  // subtracted.
  int relative_score() const {
    int v = score(0), o = score(1);
    return is_violet_turn() ? v - o : o - v;
  }

  // Heuristically evaluates the current board state. Higher values are better
  // for violet, lower values are better for orange.
  int evaluate() const { return eval_pieces() + eval_effect(); }

  // Same as evaluate(), but higher values are better for the current player.
  int nega_eval() const { return is_violet_turn() ? evaluate() : -evaluate(); }

  // Generates a list of all possible moves that could be made in the game
  // (regardless of the current game state, as this method is static).
  static std::vector<Move> all_possible_moves();

 protected:
  static bool in_bounds(int x, int y) {
    return (x >= 0 && y >= 0 && x < XSIZE && y < YSIZE);
  }

  constexpr static uint32_t PASSED = 0x80000000;
  Key key_;
  uint8_t cells[YSIZE][XSIZE] = {};
  uint32_t pieces_[2] = {0, 0};
  int turn_ = 0;
  int player_ = 0;

  bool placeable(int px, int py, const Piece* piece) const noexcept;
  int eval_pieces() const;
  int eval_effect() const;
};

namespace standard {
using Board = BoardImpl<BlokusDuoStandard>;
}
namespace mini {
using Board = BoardImpl<BlokusDuoMini>;
}
// blokusuduo::Board is an alias for the standard version.
using Board = BoardImpl<BlokusDuoStandard>;

namespace search {
// The best move found, and the score of that move.
using SearchResult = std::pair<Move, short>;

// The number of nodes visited during search. The library does not reset this
// value, so it accumulates across multiple calls to search functions.
extern int visited_nodes;

class Timeout {};

template <class Game>
SearchResult negascout(const BoardImpl<Game>& node, int max_depth, int stop_ms,
                       int timeout_ms);
template <class Game>
SearchResult wld(const BoardImpl<Game>& node, int timeout_sec);
template <class Game>
SearchResult perfect(const BoardImpl<Game>& node);

template <class Game>
Move opening_move(const BoardImpl<Game>& b);

}  // namespace search

}  // namespace blokusduo

#endif  // BLOKUSDUO_H_
