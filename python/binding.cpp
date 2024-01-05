#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "blokusduo.h"
using namespace blokusduo;
namespace nb = nanobind;

namespace {

template <class Game>
auto pieces(const BoardImpl<Game>& b, int player) {
  static bool data[Game::NUM_PIECES];
  for (int i = 0; i < Game::NUM_PIECES; ++i)
    data[i] = b.is_piece_available(player, i);
  return nb::ndarray<nb::numpy, const bool, nb::shape<Game::NUM_PIECES>>(
      data, {Game::NUM_PIECES});
}

template <class Game>
void define_blokusduo_module(nb::module_&& m) {
  // Addressable constants.
  static const int XSIZE = BoardImpl<Game>::XSIZE;
  static const int YSIZE = BoardImpl<Game>::YSIZE;
  static const int VIOLET_TILE = BoardImpl<Game>::VIOLET_TILE;
  static const int ORANGE_TILE = BoardImpl<Game>::ORANGE_TILE;

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
      .def("pieces", &pieces<Game>)
      .def("hash_key",
           [](const BoardImpl<Game>& b) {
             auto key = b.key().string_view();
             return nb::bytes(key.data(), key.size());
           })
      .def_ro_static("VIOLET_TILE", &VIOLET_TILE)
      .def_ro_static("ORANGE_TILE", &ORANGE_TILE)
      .def("at", nb::overload_cast<int, int>(&BoardImpl<Game>::at, nb::const_))
      .def("data",
           [](const BoardImpl<Game>& b) {
             return nb::ndarray<
                 nb::numpy, const uint8_t,
                 nb::shape<BoardImpl<Game>::YSIZE, BoardImpl<Game>::XSIZE>>(
                 b.data(), {BoardImpl<Game>::YSIZE, BoardImpl<Game>::XSIZE});
           })
      .def("valid_moves", &BoardImpl<Game>::valid_moves)
      .def("play_move", &BoardImpl<Game>::play_move)
      .def("__str__", &BoardImpl<Game>::to_string)
      .def("score", &BoardImpl<Game>::score)
      .def("evaluate", &BoardImpl<Game>::evaluate)
      .def_static("all_possible_moves", &BoardImpl<Game>::all_possible_moves);
  m.def("search_negascout", &blokusduo::search::negascout<Game>);
  m.def("search_wld", &blokusduo::search::wld<Game>);
  m.def("search_perfect", &blokusduo::search::perfect<Game>);
}

}  // namespace

NB_MODULE(blokusduo, m) {
  nb::class_<Move>(m, "Move")
      .def(nb::init<const char*>())
      .def("__repr__", [](Move m) { return "Move(" + m.fourcc() + ")"; })
      .def("__str__", &Move::fourcc)
      .def("__eq__", &Move::operator==)
      .def("__hash__", [](Move m) { return Move::Hash()(m); })
      .def_prop_ro("x", &Move::x)
      .def_prop_ro("y", &Move::y)
      .def_prop_ro("piece", &Move::piece)
      .def_prop_ro("orientation", &Move::orientation)
      .def_prop_ro("is_pass", &Move::is_pass)
      .def("canonicalize", &Move::canonicalize)
      .def("mirror", &Move::mirror);
  define_blokusduo_module<BlokusDuoMini>(m.def_submodule("mini"));
  define_blokusduo_module<BlokusDuoStandard>(m.def_submodule("standard"));
}
