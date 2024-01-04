#!/usr/bin/env python3

class Piece:
    def __init__(self, orientation, name, coords):
        self.orientation = orientation
        self.name = name
        self.coords = coords
        self.corners = [
            (x, y) for (x, y) in coords
            if not ((((x-1, y) in coords) and ((x+1, y) in coords)) or
                    (((x, y-1) in coords) and ((x, y+1) in coords)))
        ]
        self.directed_corners = [
            [(x, y) for (x, y) in self.corners if not (((x, y-1) in coords) or ((x-1, y) in coords))],  # north-west
            [(x, y) for (x, y) in self.corners if not (((x, y-1) in coords) or ((x+1, y) in coords))],  # north-east
            [(x, y) for (x, y) in self.corners if not (((x, y+1) in coords) or ((x-1, y) in coords))],  # south-west
            [(x, y) for (x, y) in self.corners if not (((x, y+1) in coords) or ((x+1, y) in coords))]   # south-east
        ]
        self.size = len(self.coords)
        self.min_x = min(x for (x, y) in self.coords)
        self.min_y = min(y for (x, y) in self.coords)
        self.max_x = max(x for (x, y) in self.coords)
        self.max_y = max(y for (x, y) in self.coords)

class Block:
    def __init__(self, name, *coords):
        self.name = name
        self.size = len(coords)
        self.make_variations(list(coords))

    def rotate(self, x, y, dir):
        dx, dy, piece = self.rotations[dir]
        return [x+dx, y+dy, piece]

    def __iter__(self):
        return iter(self.pieces)

    def find_overlap(self, p2):
        for p1 in self.pieces:
            mx1, my1 = p1.min_x, p1.min_y
            mx2, my2 = p2.min_x, p2.min_y
            if all((x1-mx1 == x2-mx2 and y1-my1 == y2-my2)
                    for ((x1, y1), (x2, y2))
                    in zip(sorted(p1.coords), sorted(p2.coords))):
                return [mx2-mx1, my2-my1, p1]
        return None

    def make_variations(self, coords):
        self.pieces = []
        self.rotations = []
        for i in range(8):
            piece = Piece(i, f"{self.name}{i}", list(coords))
            synonym = self.find_overlap(piece)
            if synonym:
                self.rotations.append(synonym)
            else:
                self.rotations.append([0, 0, piece])
                self.pieces.append(piece)
            coords = [(-x, y) for x, y in coords]  # mirror
            if i % 2 == 1:
                coords = [(-y, x) for x, y in coords]  # rotate right

BLOCK_SET = [
  Block('a', (0,0)), # I1
  Block('b', (0,0), (0,1)), # I2

  Block('c', (0,0), (0,1), (0,-1)), # I3
  Block('d', (0,0), (1,0), (0,-1)), # L3

  Block('e', (0,0), (0,1), (0,2), (0,-1)), # I4
  Block('f', (0,0), (0,-1), (0,1), (-1,1)), # L4
  Block('g', (0,0), (1,0), (0,1), (0,-1)), # T4
  Block('h', (0,0), (1,0), (0,1), (1,1)), # O4
  Block('i', (-1,0), (0,0), (0,1), (1,1)), # Z4

  Block('j', (0,0), (0,1), (0,2), (0,-1), (0,-2)), # I5
  Block('k', (0,0), (0,1), (0,-2), (0,-1), (-1,1)), # L5
  Block('l', (0,-2), (0,-1), (0,0), (-1,0), (-1,1)), # N5
  Block('m', (0,-1), (-1,0), (0,0), (-1,1), (0,1)), # P5
  Block('n', (0,0), (0,1), (-1,1), (0,-1), (-1,-1)), # C5
  Block('o', (0,-1), (0,0), (1,0), (0,1), (0,2)), # Y5
  Block('p', (0,0), (0,-1), (0,1), (-1,1), (1,1)), # T5
  Block('q', (0,0), (1,0), (2,0), (0,-1), (0,-2)), # V5
  Block('r', (0,0), (1,0), (1,1), (0,-1), (-1,-1)), # W5
  Block('s', (0,0), (1,0), (1,1), (-1,0), (-1,-1)), # Z5
  Block('t', (-1,-1), (-1,0), (0,0), (1,0), (0,1)), # F5
  Block('u', (0,0), (1,0), (0,1), (-1,0), (0,-1)), # X5
]

def to_c(obj):
    if isinstance(obj, list) or isinstance(obj, tuple):
        return '{' + ','.join(map(to_c, obj)) + '}'
    else:
        return str(obj)

def generate_cpp():
    print('#include "piece.h"')
    print()
    print('namespace blokusduo {')
    print()
    print('namespace {')
    print()
    pieces = []
    for id, blk in enumerate(BLOCK_SET):
        pieces.insert(0, [])
        for piece in blk:
            fs = ["0x%02x" % (id << 3 | piece.orientation), piece.size, to_c(piece.coords),
                  to_c([len(corner) for corner in piece.directed_corners]),
                  to_c(piece.directed_corners),
                  piece.min_x, piece.min_y, piece.max_x, piece.max_y]
            print(f"const Piece {piece.name} = {{{', '.join(map(str, fs))}}};")
            pieces[0].append(('&' + piece.name, piece.size))
    pieces = [item for sublist in pieces for item in sublist]
    print()
    print('}  // namespace')
    print()
    print("const std::array<const Piece*, BlokusDuoMini::NUM_ORIENTED_PIECES> BlokusDuoMini::piece_set = {")
    print(f"  {', '.join([e for (e, n) in pieces if n <= 4])}")
    print("};")
    print()
    print("const std::array<const Piece*, BlokusDuoStandard::NUM_ORIENTED_PIECES> BlokusDuoStandard::piece_set = {")
    print(f"  {', '.join([e for (e, _) in pieces])}")
    print("};")
    print()
    print("const Block block_set[] = {")
    for blk in BLOCK_SET:
        print("  {")
        print(f"    '{blk.name}', {blk.size},")
        print(f"    {{ {', '.join(['&'+p.name for p in blk])}, nullptr }},")
        rot = ', '.join([f"{{{dx},{dy},&{piece.name}}}" for dx, dy, piece in [blk.rotate(0,0,dir) for dir in range(8)]])
        print(f"    {{ {rot} }}")
        print("  },")
    print("};")
    print()
    print('}  // namespace blokusduo')

if __name__ == "__main__":
    generate_cpp()
