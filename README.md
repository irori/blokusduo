# libblokusduo

libblokusduo is a C++20 library for playing and searching the board game
Blokus Duo. It provides board-state management, legal move generation,
position evaluation, and several game-tree search algorithms. Python bindings
are available through [nanobind](https://github.com/wjakob/nanobind).

This library is based on the program that won the
[GPCC Computer Blokus Duo tournament](https://prosym.org/gpcc/gpcc09.htm#g3)
in each of 2007, 2008, and 2009.

## Features

- Standard Blokus Duo and a smaller Mini variant
- Legal move generation, move application, game-over detection, and scoring
- Board occupancy, remaining-piece, and position-key access
- NegaScout, win/loss/draw, and perfect endgame searches
- A heuristic evaluation based on placed pieces and available space
- Optional native CPU and WebAssembly fixed-width SIMD optimizations

| Variant | Board | Pieces per player | Starting points (zero-based) | C++ type | Python module |
| --- | ---: | ---: | --- | --- | --- |
| Standard | 14×14 | 21 | Violet `(4, 4)`, Orange `(9, 9)` | `standard::Board` | `blokusduo.standard` |
| Mini | 8×8 | 9 | Violet `(2, 2)`, Orange `(5, 5)` | `mini::Board` | `blokusduo.mini` |

The Mini variant uses the standard set with all pentominoes removed. Violet is
player 0, Orange is player 1, and Violet moves first.

## Building

CMake 3.18 or later, Python 3.9 or later, and a C++20 compiler are required.
Python is also used at build time to generate the piece definitions, even when
the Python bindings are disabled.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The `search_benchmark` executable is always built. If GoogleTest is available,
CMake also builds and registers `board_test`.

```bash
ctest --test-dir build --output-on-failure
./build/search_benchmark
```

### CPU-specific optimizations

CPU-specific optimization is enabled by default with
`BLOKUSDUO_ENABLE_NATIVE=ON`. This uses `-march=native` with GCC and Clang, or
`/arch:AVX2` with MSVC. Disable it when cross-compiling or when the resulting
binary must run on CPUs other than the build host:

```bash
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBLOKUSDUO_ENABLE_NATIVE=OFF
```

For an Emscripten build using
[WebAssembly fixed-width SIMD (SIMD128)](https://emscripten.org/docs/porting/simd.html),
enable `BLOKUSDUO_ENABLE_WASM_SIMD`. The resulting module requires a runtime
with fixed-width SIMD support:

```bash
emcmake cmake -B build-wasm -S . \
  -DBLOKUSDUO_ENABLE_WASM_SIMD=ON
cmake --build build-wasm
```

## Using the C++ library

Add the library to your CMake project and link the `blokusduo` target:

```cmake
add_subdirectory(path/to/libblokusduo)
target_link_libraries(your_target PRIVATE blokusduo)
```

The following example creates a standard board and plays its first generated
legal move:

```cpp
#include <iostream>

#include <blokusduo.h>

int main() {
  blokusduo::standard::Board board;
  const auto moves = board.valid_moves();
  board.play_move(moves.front());

  std::cout << "move: " << moves.front().code() << '\n';
  std::cout << board.to_string();
}
```

`blokusduo::Board` is an alias for `blokusduo::standard::Board`. See
[`include/blokusduo.h`](include/blokusduo.h) for the complete public C++ API.

## Using the Python bindings

Install nanobind and NumPy, then configure the project with `BUILD_PYTHON=ON`:

```bash
python -m pip install nanobind numpy
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_PYTHON=ON
cmake --build build
cmake --install build
```

This example lets the NegaScout search play both sides:

```python
import blokusduo

game = blokusduo.standard
board = game.Board()
max_depth = 4  # Look ahead by up to four plies (half-moves).

while not board.is_game_over():
    move, value = game.search_negascout(
        board,
        max_depth,
        lambda depth, result: True,
    )
    board.play_move(move)
    print(f"turn={board.turn:2d} move={move} value={value}")

print(board)
print("Violet:", board.score(0))
print("Orange:", board.score(1))
```

`Board.occupancy()` returns an independent Boolean NumPy array with shape
`(2, YSIZE, XSIZE)`. `Board.available_pieces()` similarly returns a snapshot
with shape `(2, NUM_PIECES)`. Modifying either returned array does not modify
the board.

```python
occupied = board.occupancy()
remaining = board.available_pieces()

assert occupied[0, 4, 4] == board.has_tile(0, 4, 4)
assert remaining[1, 3] == board.is_piece_available(1, 3)
```

## Board API

A board object stores a complete game position. The most commonly used
operations are:

| API | Description |
| --- | --- |
| `player()` / `opponent()` | Return the player to move and the other player |
| `turn()` | Return the number of moves played, including passes |
| `valid_moves()` | Return all legal moves in the current position |
| `visit_moves(visitor)` | Visit legal moves without allocating a result vector (C++ only) |
| `is_valid_move(move)` | Test whether a placement is legal |
| `play_move(move)` | Apply a move to the current board |
| `child(move)` | Return a copy with a move applied |
| `clone()` | Return an independent copy of the board (Python only) |
| `is_game_over()` | Test whether both players have passed |
| `score(player)` | Return the number of tiles placed by a player |
| `has_tile(player, x, y)` | Test whether a player occupies a square |
| `key()` / `hash_key()` | Return the compact position key in C++ / Python |

When no piece can be placed, `valid_moves()` returns a single pass move.

`play_move()` does not validate its argument. In addition,
`is_valid_move(Move::pass())` returns `true` even when placements are
available. Applications should normally apply a move returned by
`valid_moves()`.

### Move notation

`Move` reads and writes the four-character notation described in the
[ICFPT 2014 Blokus specification](http://www.icfpt2014.org/Info.asp?call=57B842DF9E6A0F4206826BB4449D5355).
For example, `56t2` consists of:

- an x coordinate and a y coordinate, written as one-based hexadecimal digits;
- a piece identifier (`a` through `u` in the standard game); and
- an orientation identifier (`0` through `7`).

The linked specification defines the piece identifiers, reference points, and
orientation numbering. The `x()` and `y()` C++ accessors, and the corresponding
Python properties, return zero-based coordinates.

A pass is written as `0000`. The parser accepts both `0000` and `----`.
Symmetric pieces can have more than one code for the same placement;
`canonicalize()` returns the representation with the canonical orientation.

`Board::all_possible_moves()` returns every on-board placement plus a pass,
without considering a particular position. `Board::rotate_move()` transforms a
move using one of the board's eight symmetries, which is useful for data
augmentation and symmetry-aware position processing.

## Evaluation function

`Board::evaluate()` returns an integer from Violet's point of view: larger
values favor Violet and smaller values favor Orange. It combines two terms:

```text
evaluation = placed-piece value + influence(Violet) - influence(Orange)
```

### Placed-piece value

Placed pieces receive the following weights. Violet's pieces add to the
evaluation and Orange's pieces subtract from it.

| Pieces | Number of tiles | Value |
| --- | ---: | ---: |
| `a` | 1 | 2 |
| `b` | 2 | 4 |
| `c`–`d` | 3 | 6 |
| `e`–`i` | 4 | 10 |
| `j`–`u` | 5 | 16 |

These weights value playing larger pieces more highly than a simple count of
placed tiles would.

### Influence

Influence estimates the amount of nearby space each player can still use. It
starts from the player's currently usable corners and counts open squares
reachable within three orthogonal steps, excluding occupied squares and
squares that cannot be used because they share an edge with that player's
tiles. Before a player has moved, the player's starting point is used.

The influence term is an approximation intended for move selection; it is not
part of the game score.

In C++, `nega_eval()` converts the same evaluation to the current player's
point of view. Python exposes only the Violet-oriented `evaluate()`. To obtain
the library's final placed-tile difference, use `score(0) - score(1)`.

## Search algorithms

Every search function returns `(best_move, value)`. The value is from the point
of view of the player to move in the root position: positive is favorable and
negative is unfavorable.

| C++ | Python | Recommended use | Returned value |
| --- | --- | --- | --- |
| `search::negascout(board, max_depth, callback)` | `search_negascout(board, max_depth, callback)` | Opening and middlegame | Heuristic value at the search horizon |
| `search::negascout_gumbel(board, max_depth, temperature, seed, callback)` | `search_negascout_gumbel(board, max_depth, temperature, seed, callback)` | Randomized opening and middlegame play | Heuristic value at the search horizon |
| `search::wld(board)` | `search_wld(board)` | Late endgame | Positive for a win, zero for a draw, negative for a loss |
| `search::perfect(board)` | `search_perfect(board)` | Final endgame | Exact final placed-tile difference |

### NegaScout

NegaScout, also known as Principal Variation Search, is a depth-limited
alpha-beta search. This implementation uses:

- iterative deepening from depth 2 through `max_depth`;
- a transposition table and the previous iteration for move ordering;
- heuristic and piece-size move ordering; and
- ProbCut to prune branches based on shallower searches.

For performance, the Standard search omits one- through four-tile pieces from
its candidates during the first eight turns. This affects only NegaScout's
choice of candidates; `Board::valid_moves()` still returns every legal move.

`max_depth` must be at least 2. The callback receives `(depth, result)` after
each completed iteration. Return `false` to keep that result and stop before
the next iteration.

```cpp
#include <chrono>
#include <iostream>

using namespace std::chrono;

const auto deadline = steady_clock::now() + milliseconds(500);
auto [move, value] = blokusduo::search::negascout(
    board, 8,
    [&](int depth, blokusduo::search::SearchResult result) {
      std::cout << "depth=" << depth << " value=" << result.second << '\n';
      return steady_clock::now() < deadline;
    });
```

The callback is invoked only between completed iterations. It cannot interrupt
an iteration already in progress, so it provides a soft rather than a strict
time limit.

`negascout_gumbel()` adds Gumbel noise to the root-move scores.
`temperature` controls the amount of variation in evaluation score units; at
a completed depth, moves are sampled in proportion to
`exp(score / temperature)`. Zero is identical to `negascout()`. Using the same
`seed` makes the result reproducible. The returned value is the selected move's
original score, without noise.

As a rough guide, use `0.5` for subtle variation, `1.0` for mild randomness,
and `2.0` for more visible variety. At `4.0` or higher, substantially weaker
moves may be selected.

```cpp
auto [move, value] = blokusduo::search::negascout_gumbel(
    board, 8, 1.5, seed,
    [](int, blokusduo::search::SearchResult) { return true; });
```

### Win/loss/draw and perfect searches

`wld()` searches to the end of the game while distinguishing only wins, draws,
and losses. It can stop after proving a winning continuation, so it may be
faster than determining the exact final margin.

`perfect()` searches to the end of the game and returns the best exact final
placed-tile difference. Both searches use alpha-beta pruning and position
caching. Their cost grows quickly with the number of remaining moves, so they
are intended for endgame positions.

In C++, `search::visited_nodes` is an accumulating node counter. Search
functions do not reset it; assign zero before a call when measuring one search.

[`src/search_benchmark.cpp`](src/search_benchmark.cpp) contains an example that
switches from NegaScout to win/loss/draw search and then to perfect search as
the game progresses. Its thresholds are examples and should be tuned for the
available CPU time and desired playing strength.

## C++ and Python API mapping

`Move` is defined at the Python module's top level. Board types and search
functions are provided by the `blokusduo.standard` and `blokusduo.mini`
submodules.

| Feature | C++ | Python |
| --- | --- | --- |
| Move | `blokusduo::Move` | `blokusduo.Move` |
| Standard board | `blokusduo::standard::Board` | `blokusduo.standard.Board` |
| Mini board | `blokusduo::mini::Board` | `blokusduo.mini.Board` |
| Legal moves | `board.valid_moves()` | `board.valid_moves()` |
| Occupancy | Query with `has_tile()` | `board.occupancy()` |
| Remaining pieces | Query with `is_piece_available()` | `board.available_pieces()` |
| Board copy | Copy constructor or `child()` | `clone()` or `child()` |
| Evaluation | `evaluate()` or `nega_eval()` | `evaluate()` |
| Search | `blokusduo::search::*` | `search_*` in each variant submodule |

## License

See [`LICENSE`](LICENSE).
