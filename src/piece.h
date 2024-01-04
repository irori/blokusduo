#ifndef PIECE_H_
#define PIECE_H_

#include "blokusduo.h"

namespace blokusduo {

constexpr int NUM_ORIENTATIONS = 8;

// Represents a piece with a fixed orientation.
struct Piece {
  struct Coords {
    int x, y;
  };

  int id;  // block_id << 3 | orientation
  int size;

  // Relative coordinates of each cell from the piece origin.
  Coords coords[5];

  // Corner cells for each orientation (NW, NE, SE, SW).
  int nr_corners[4];
  Coords corners[4][3];

  // Bounding box.
  int minx, miny, maxx, maxy;

  int block_id() const { return id >> 3; }
  int orientation() const { return id & 0x7; }
};

struct Block {
  struct Rotation {
    int offset_x, offset_y;
    const Piece *piece;
  };

  char name;
  int size;
  const Piece *variations[9];  // null-terminated
  const Rotation rotations[NUM_ORIENTATIONS];
};

extern const Block block_set[];

}  // namespace blokusduo

#endif  // PIECE_H_
