import unittest

import numpy as np

import blokusduo


class BoardTest(unittest.TestCase):
    def test_occupancy_is_an_owned_snapshot(self):
        for game in (blokusduo.mini, blokusduo.standard):
            board = game.Board()
            occupancy = board.occupancy()
            self.assertEqual((2, board.YSIZE, board.XSIZE), occupancy.shape)
            self.assertEqual(np.bool_, occupancy.dtype)
            self.assertFalse(occupancy.any())

            board.play_move(board.valid_moves()[0])
            occupancy = board.occupancy()
            self.assertEqual(board.score(0), occupancy[0].sum())

            occupancy[:] = False
            self.assertTrue(board.occupancy().any())

    def test_available_pieces_is_an_owned_snapshot(self):
        for game in (blokusduo.mini, blokusduo.standard):
            board = game.Board()
            board.play_move(board.valid_moves()[0])
            available = board.available_pieces()
            self.assertEqual((2, game.NUM_PIECES), available.shape)
            self.assertEqual(np.bool_, available.dtype)
            self.assertEqual(game.NUM_PIECES - 1, available[0].sum())
            self.assertEqual(game.NUM_PIECES, available[1].sum())

            available[:] = True
            self.assertEqual(
                game.NUM_PIECES - 1, board.available_pieces()[0].sum()
            )

    def test_gumbel_search_is_reproducible(self):
        board = blokusduo.mini.Board()
        callback = lambda depth, result: True
        result = blokusduo.mini.search_negascout_gumbel(
            board, 3, 4.0, 1234, callback
        )
        self.assertEqual(
            result,
            blokusduo.mini.search_negascout_gumbel(
                board, 3, 4.0, 1234, callback
            ),
        )
        self.assertTrue(board.is_valid_move(result[0]))


if __name__ == "__main__":
    unittest.main()
