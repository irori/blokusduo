#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "blokusduo.h"
using namespace blokusduo;
namespace nb = nanobind;

namespace {

template <class Game>
auto available_pieces(const BoardImpl<Game>& b) {
  constexpr int SIZE = 2 * Game::NUM_PIECES;
  bool* data = new bool[SIZE];
  for (int player = 0; player < 2; player++) {
    for (int piece = 0; piece < Game::NUM_PIECES; piece++) {
      data[player * Game::NUM_PIECES + piece] =
          b.is_piece_available(player, piece);
    }
  }
  nb::capsule owner(data,
                    [](void* p) noexcept { delete[] static_cast<bool*>(p); });
  return nb::ndarray<nb::numpy, bool, nb::shape<2, Game::NUM_PIECES>>(
      data, {2, Game::NUM_PIECES}, owner);
}

template <class Game>
auto occupancy(const BoardImpl<Game>& b) {
  constexpr int SIZE = 2 * Game::YSIZE * Game::XSIZE;
  bool* data = new bool[SIZE];
  for (int player = 0; player < 2; player++) {
    for (int y = 0; y < Game::YSIZE; y++) {
      for (int x = 0; x < Game::XSIZE; x++) {
        data[(player * Game::YSIZE + y) * Game::XSIZE + x] =
            b.has_tile(player, x, y);
      }
    }
  }
  nb::capsule owner(data,
                    [](void* p) noexcept { delete[] static_cast<bool*>(p); });
  return nb::ndarray<nb::numpy, bool,
                     nb::shape<2, Game::YSIZE, Game::XSIZE>>(
      data, {2, Game::YSIZE, Game::XSIZE}, owner);
}

template <class Game>
void define_blokusduo_module(nb::module_&& m) {
  // Addressable constants.
  static const int XSIZE = BoardImpl<Game>::XSIZE;
  static const int YSIZE = BoardImpl<Game>::YSIZE;

  m.attr("NUM_PIECES") = Game::NUM_PIECES;
  nb::class_<BoardImpl<Game>>(m, "Board")
      .def_ro_static("XSIZE", &XSIZE)
      .def_ro_static("YSIZE", &YSIZE)
      .def(nb::init<>())
      .def("clone", [](const BoardImpl<Game>& b) { return b; })
      .def_prop_ro("player", &BoardImpl<Game>::player)
      .def_prop_ro("opponent", &BoardImpl<Game>::opponent)
      .def_prop_ro("turn", &BoardImpl<Game>::turn)
      .def("is_game_over", &BoardImpl<Game>::is_game_over)
      .def("is_valid_move", &BoardImpl<Game>::is_valid_move)
      .def("is_piece_available", &BoardImpl<Game>::is_piece_available)
      .def("did_pass", &BoardImpl<Game>::did_pass)
      .def("available_pieces", &available_pieces<Game>)
      .def("hash_key",
           [](const BoardImpl<Game>& b) {
             auto key = b.key().string_view();
             return nb::bytes(key.data(), key.size());
           })
      .def("has_tile", &BoardImpl<Game>::has_tile)
      .def("occupancy", &occupancy<Game>)
      .def("valid_moves", &BoardImpl<Game>::valid_moves)
      .def("play_move", &BoardImpl<Game>::play_move)
      .def("child", &BoardImpl<Game>::child)
      .def("__str__", &BoardImpl<Game>::to_string)
      .def("score", &BoardImpl<Game>::score)
      .def("evaluate", &BoardImpl<Game>::evaluate)
      .def_static("all_possible_moves", &BoardImpl<Game>::all_possible_moves)
      .def_static("rotate_move", &BoardImpl<Game>::rotate_move);
  m.def("search_negascout", &blokusduo::search::negascout<Game>);
  m.def("search_wld", &blokusduo::search::wld<Game>);
  m.def("search_perfect", &blokusduo::search::perfect<Game>);
}

}  // namespace

NB_MODULE(blokusduo, m) {
  nb::class_<Move>(m, "Move")
      .def(nb::init<const char*>())
      .def("__repr__", [](Move m) { return "Move(" + m.code() + ")"; })
      .def("__str__", &Move::code)
      .def("__eq__", &Move::operator==)
      .def("__hash__", [](Move m) { return Move::Hash()(m); })
      .def_prop_ro("x", &Move::x)
      .def_prop_ro("y", &Move::y)
      .def_prop_ro("piece", &Move::piece)
      .def_prop_ro("orientation", &Move::orientation)
      .def_prop_ro("is_pass", &Move::is_pass)
      .def("canonicalize", &Move::canonicalize);
  define_blokusduo_module<BlokusDuoMini>(m.def_submodule("mini"));
  define_blokusduo_module<BlokusDuoStandard>(m.def_submodule("standard"));
}
