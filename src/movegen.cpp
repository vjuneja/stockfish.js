/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2017 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cassert>

#include "movegen.h"
#include "position.h"

namespace {

  template<Variant V, CastlingRight Cr, bool Checks, bool Chess960>
  ExtMove* generate_castling(const Position& pos, ExtMove* moveList, Color us) {

    static const bool KingSide = (Cr == WHITE_OO || Cr == BLACK_OO);

    if (pos.castling_impeded(Cr) || !pos.can_castle(Cr))
        return moveList;

    // After castling, the rook and king final positions are the same in Chess960
    // as they would be in standard chess.
    Square kfrom = pos.square<KING>(us);
#ifdef ANTI
    if (V == ANTI_VARIANT)
        kfrom = pos.castling_king_square(Cr);
#endif
    Square rfrom = pos.castling_rook_square(Cr);
    Square kto = relative_square(us, KingSide ? SQ_G1 : SQ_C1);
    Bitboard enemies = pos.pieces(~us);

    assert(!pos.checkers());

    const Square K = Chess960 ? kto > kfrom ? WEST : EAST
                              : KingSide    ? WEST : EAST;

#ifdef ANTI
    if (V != ANTI_VARIANT)
    {
#endif
    for (Square s = kto; s != kfrom; s += K)
#ifdef ATOMIC
        if (V == ATOMIC_VARIANT)
        {
            if (   !(pos.attacks_from<KING>(pos.square<KING>(~us)) & s)
                &&  (pos.attackers_to(s, pos.pieces() ^ kfrom) & enemies))
                return moveList;
        }
        else
#endif
        if (pos.attackers_to(s) & enemies)
            return moveList;

    // Because we generate only legal castling moves we need to verify that
    // when moving the castling rook we do not discover some hidden checker.
    // For instance an enemy queen in SQ_A1 when castling rook is in SQ_B1.
    if (Chess960 && (attacks_bb<ROOK>(kto, pos.pieces() ^ rfrom) & pos.pieces(~us, ROOK, QUEEN)))
    {
#ifdef ATOMIC
        if (V == ATOMIC_VARIANT && (pos.attacks_from<KING>(pos.square<KING>(~us)) & kto)) {} else
#endif
        return moveList;
    }
#ifdef ANTI
    }
#endif

    Move m = make<CASTLING>(kfrom, rfrom);

    if (Checks && !pos.gives_check(m))
        return moveList;

    *moveList++ = m;
    return moveList;
  }


  template<Variant V, GenType Type, Square D>
  ExtMove* make_promotions(ExtMove* moveList, Square to, Square ksq) {

#ifdef ANTI
    if (V == ANTI_VARIANT)
    {
        if (Type == QUIETS || Type == CAPTURES || Type == NON_EVASIONS)
        {
            *moveList++ = make<PROMOTION>(to - D, to, QUEEN);
            *moveList++ = make<PROMOTION>(to - D, to, ROOK);
            *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
            *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
            *moveList++ = make<PROMOTION>(to - D, to, KING);
        }
        return moveList;
    }
#endif
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
        *moveList++ = make<PROMOTION>(to - D, to, QUEEN);

    if (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS)
    {
        *moveList++ = make<PROMOTION>(to - D, to, ROOK);
        *moveList++ = make<PROMOTION>(to - D, to, BISHOP);
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
    }

    // Knight promotion is the only promotion that can give a direct check
    // that's not already included in the queen promotion.
    if (Type == QUIET_CHECKS && (PseudoAttacks[KNIGHT][to] & ksq))
        *moveList++ = make<PROMOTION>(to - D, to, KNIGHT);
    else
        (void)ksq; // Silence a warning under MSVC

    return moveList;
  }

#ifdef CRAZYHOUSE
  template<Color Us, PieceType Pt, bool Checks>
  ExtMove* generate_drops(const Position& pos, ExtMove* moveList, Bitboard b) {
    if (pos.count_in_hand<Pt>(Us))
    {
        if (Checks)
            b &= pos.check_squares(Pt);
        while (b)
            *moveList++ = make_drop(pop_lsb(&b), make_piece(Us, Pt));
    }

    return moveList;
  }
#endif

  template<Variant V, Color Us, GenType Type>
  ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList, Bitboard target) {

    // Compute our parametrized parameters at compile time, named according to
    // the point of view of white side.
    const Color    Them     = (Us == WHITE ? BLACK      : WHITE);
    const Bitboard TRank8BB = (Us == WHITE ? Rank8BB    : Rank1BB);
    const Bitboard TRank7BB = (Us == WHITE ? Rank7BB    : Rank2BB);
#ifdef HORDE
    const Bitboard TRank2BB = (Us == WHITE ? Rank2BB    : Rank7BB);
#endif
    const Bitboard TRank3BB = (Us == WHITE ? Rank3BB    : Rank6BB);
    const Square   Up       = (Us == WHITE ? NORTH      : SOUTH);
    const Square   Right    = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    const Square   Left     = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    Bitboard emptySquares;

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) &  TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    Bitboard enemies = (Type == EVASIONS ? pos.pieces(Them) & target:
                        Type == CAPTURES ? target : pos.pieces(Them));

    // Single and double pawn pushes, no promotions
    if (Type != CAPTURES)
    {
        emptySquares = (Type == QUIETS || Type == QUIET_CHECKS ? target : ~pos.pieces());
#ifdef ANTI
        if (V == ANTI_VARIANT)
            emptySquares &= target;
#endif

        Bitboard b1 = shift<Up>(pawnsNotOn7)   & emptySquares;
        Bitboard b2 = shift<Up>(b1 & TRank3BB) & emptySquares;
#ifdef HORDE
        if (V == HORDE_VARIANT)
            b2 = shift<Up>(b1 & (TRank2BB | TRank3BB)) & emptySquares;
#endif

#ifdef LOSERS
        if (V == LOSERS_VARIANT)
        {
            b1 &= target;
            b2 &= target;
        }
#endif
        if (Type == EVASIONS) // Consider only blocking squares
        {
            b1 &= target;
            b2 &= target;
        }

        if (Type == QUIET_CHECKS)
        {
            Square ksq = pos.square<KING>(Them);

            b1 &= pos.attacks_from<PAWN>(ksq, Them);
            b2 &= pos.attacks_from<PAWN>(ksq, Them);

            // Add pawn pushes which give discovered check. This is possible only
            // if the pawn is not on the same file as the enemy king, because we
            // don't generate captures. Note that a possible discovery check
            // promotion has been already generated amongst the captures.
            Bitboard dcCandidates = pos.discovered_check_candidates();
            if (pawnsNotOn7 & dcCandidates)
            {
                Bitboard dc1 = shift<Up>(pawnsNotOn7 & dcCandidates) & emptySquares & ~file_bb(ksq);
                Bitboard dc2 = shift<Up>(dc1 & TRank3BB) & emptySquares;

                b1 |= dc1;
                b2 |= dc2;
            }
        }

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - Up, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - Up - Up, to);
        }
    }

    // Promotions and underpromotions
    if (pawnsOn7 && (Type != EVASIONS || (target & TRank8BB)))
    {
        if (Type == CAPTURES)
        {
            emptySquares = ~pos.pieces();
#ifdef ATOMIC
            // Promotes only if promotion wins or explodes checkers
            if (V == ATOMIC_VARIANT && pos.checkers())
                emptySquares &= target;
#endif
        }
#ifdef ANTI
        if (V == ANTI_VARIANT)
            emptySquares &= target;
#endif
#ifdef LOSERS
        if (V == LOSERS_VARIANT)
            emptySquares &= target;
#endif

        if (Type == EVASIONS)
            emptySquares &= target;

        Bitboard b1 = shift<Right>(pawnsOn7) & enemies;
        Bitboard b2 = shift<Left >(pawnsOn7) & enemies;
        Bitboard b3 = shift<Up   >(pawnsOn7) & emptySquares;

        Square ksq = pos.square<KING>(Them);

        while (b1)
            moveList = make_promotions<V, Type, Right>(moveList, pop_lsb(&b1), ksq);

        while (b2)
            moveList = make_promotions<V, Type, Left >(moveList, pop_lsb(&b2), ksq);

        while (b3)
            moveList = make_promotions<V, Type, Up   >(moveList, pop_lsb(&b3), ksq);
    }

    // Standard and en-passant captures
    if (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
    {
        Bitboard b1 = shift<Right>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<Left >(pawnsNotOn7) & enemies;

        while (b1)
        {
            Square to = pop_lsb(&b1);
            *moveList++ = make_move(to - Right, to);
        }

        while (b2)
        {
            Square to = pop_lsb(&b2);
            *moveList++ = make_move(to - Left, to);
        }

        if (pos.ep_square() != SQ_NONE)
        {
            assert(rank_of(pos.ep_square()) == relative_rank(Us, RANK_6));

            // An en passant capture can be an evasion only if the checking piece
            // is the double pushed pawn and so is in the target. Otherwise this
            // is a discovery check and we are forced to do otherwise.
            if (Type == EVASIONS && !(target & (pos.ep_square() - Up)))
                return moveList;

            b1 = pawnsNotOn7 & pos.attacks_from<PAWN>(pos.ep_square(), Them);

            assert(b1);

            while (b1)
                *moveList++ = make<ENPASSANT>(pop_lsb(&b1), pos.ep_square());
        }
    }

    return moveList;
  }


  template<Variant V, PieceType Pt, bool Checks>
  ExtMove* generate_moves(const Position& pos, ExtMove* moveList, Color us,
                          Bitboard target) {

    assert(Pt != KING && Pt != PAWN);

    const Square* pl = pos.squares<Pt>(us);

    for (Square from = *pl; from != SQ_NONE; from = *++pl)
    {
        if (Checks)
        {
            if (    (Pt == BISHOP || Pt == ROOK || Pt == QUEEN)
                && !(PseudoAttacks[Pt][from] & target & pos.check_squares(Pt)))
                continue;

            if (pos.discovered_check_candidates() & from)
                continue;
        }

        Bitboard b = pos.attacks_from<Pt>(from) & target;
#ifdef RELAY
        if (V == RELAY_VARIANT)
        {
            const Bitboard defenders = pos.attackers_to(from) & pos.pieces(us);
            if (defenders & pos.pieces(KNIGHT))
                b |= pos.attacks_from<KNIGHT>(from) & target;
            if (defenders & pos.pieces(QUEEN, BISHOP))
                b |= pos.attacks_from<BISHOP>(from) & target;
            if (defenders & pos.pieces(QUEEN, ROOK))
                b |= pos.attacks_from<ROOK>(from) & target;
            if (defenders & pos.pieces(KING))
                b |= pos.attacks_from<KING>(from) & target;
        }
#endif

        if (Checks)
            b &= pos.check_squares(Pt);

        while (b)
            *moveList++ = make_move(from, pop_lsb(&b));
    }

    return moveList;
  }


  template<Variant V, Color Us, GenType Type>
  ExtMove* generate_all(const Position& pos, ExtMove* moveList, Bitboard target) {

    const bool Checks = Type == QUIET_CHECKS;

    moveList = generate_pawn_moves<V, Us, Type>(pos, moveList, target);
    moveList = generate_moves<V, KNIGHT, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V, BISHOP, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V,   ROOK, Checks>(pos, moveList, Us, target);
    moveList = generate_moves<V,  QUEEN, Checks>(pos, moveList, Us, target);
#ifdef CRAZYHOUSE
    if (V == CRAZYHOUSE_VARIANT && Type != CAPTURES && pos.count_in_hand<ALL_PIECES>(Us))
    {
        Bitboard b = Type == EVASIONS ? target ^ pos.checkers() :
                     Type == NON_EVASIONS ? target ^ pos.pieces(~Us) : target;
        moveList = generate_drops<Us,   PAWN, Checks>(pos, moveList, b & ~(Rank1BB | Rank8BB));
        moveList = generate_drops<Us, KNIGHT, Checks>(pos, moveList, b);
        moveList = generate_drops<Us, BISHOP, Checks>(pos, moveList, b);
        moveList = generate_drops<Us,   ROOK, Checks>(pos, moveList, b);
        moveList = generate_drops<Us,  QUEEN, Checks>(pos, moveList, b);
    }
#endif

#ifdef ANTI
    if (V == ANTI_VARIANT)
    {
        Bitboard kings = pos.pieces(Us, KING);
        while (kings)
        {
            Square ksq = pop_lsb(&kings);
            Bitboard b = pos.attacks_from<KING>(ksq) & target;
            while (b)
                *moveList++ = make_move(ksq, pop_lsb(&b));
        }
        if (pos.can_capture())
            return moveList;
    }
    else
#endif
    if (Type != QUIET_CHECKS && Type != EVASIONS)
    {
        Square ksq = pos.square<KING>(Us);
        Bitboard b = pos.attacks_from<KING>(ksq) & target;
#ifdef RACE
        if (V == RACE_VARIANT)
        {
            if (Type == CAPTURES)
                b |= pos.attacks_from<KING>(ksq) & passed_pawn_mask(WHITE, ksq) & ~pos.pieces();
            if (Type == QUIETS)
                b &= ~passed_pawn_mask(WHITE, ksq);
        }
#endif
#ifdef RELAY
        if (V == RELAY_VARIANT)
        {
            const Bitboard defenders = pos.attackers_to(ksq) & pos.pieces(Us);
            if (defenders & pos.pieces(KNIGHT))
                b |= pos.attacks_from<KNIGHT>(ksq) & target;
            if (defenders & pos.pieces(QUEEN, BISHOP))
                b |= pos.attacks_from<BISHOP>(ksq) & target;
            if (defenders & pos.pieces(QUEEN, ROOK))
                b |= pos.attacks_from<ROOK>(ksq) & target;
        }
#endif
        while (b)
            *moveList++ = make_move(ksq, pop_lsb(&b));
    }

#ifdef LOSERS
    if (V == LOSERS_VARIANT && pos.can_capture_losers())
        return moveList;
#endif
    if (Type != CAPTURES && Type != EVASIONS && pos.can_castle(Us))
    {
        if (pos.is_chess960())
        {
            moveList = generate_castling<V, MakeCastling<Us,  KING_SIDE>::right, Checks, true>(pos, moveList, Us);
            moveList = generate_castling<V, MakeCastling<Us, QUEEN_SIDE>::right, Checks, true>(pos, moveList, Us);
        }
        else
        {
            moveList = generate_castling<V, MakeCastling<Us,  KING_SIDE>::right, Checks, false>(pos, moveList, Us);
            moveList = generate_castling<V, MakeCastling<Us, QUEEN_SIDE>::right, Checks, false>(pos, moveList, Us);
        }
    }

    return moveList;
  }

} // namespace


/// generate<CAPTURES> generates all pseudo-legal captures and queen
/// promotions. Returns a pointer to the end of the move list.
///
/// generate<QUIETS> generates all pseudo-legal non-captures and
/// underpromotions. Returns a pointer to the end of the move list.
///
/// generate<NON_EVASIONS> generates all pseudo-legal captures and
/// non-captures. Returns a pointer to the end of the move list.

template<GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList) {

  assert(Type == CAPTURES || Type == QUIETS || Type == NON_EVASIONS);
  assert(!pos.checkers());

  Color us = pos.side_to_move();

  Bitboard target =  Type == CAPTURES     ?  pos.pieces(~us)
                   : Type == QUIETS       ? ~pos.pieces()
                   : Type == NON_EVASIONS ? ~pos.pieces(us) : 0;

#ifdef ANTI
  if (pos.is_anti())
  {
      if (pos.can_capture())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<ANTI_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<ANTI_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef ATOMIC
  if (pos.is_atomic())
  {
      if (Type == CAPTURES)
          target &= ~pos.attacks_from<KING>(pos.square<KING>(us));
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<ATOMIC_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<HORDE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef LOSERS
  if (pos.is_losers())
  {
      if (pos.can_capture_losers())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<LOSERS_VARIANT, BLACK, Type>(pos, moveList, target);
  }
#endif
#ifdef RACE
  if (pos.is_race())
      return us == WHITE ? generate_all<RACE_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<RACE_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
#ifdef RELAY
  if (pos.is_relay())
      return us == WHITE ? generate_all<RELAY_VARIANT, WHITE, Type>(pos, moveList, target)
                         : generate_all<RELAY_VARIANT, BLACK, Type>(pos, moveList, target);
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, Type>(pos, moveList, target)
                     : generate_all<CHESS_VARIANT, BLACK, Type>(pos, moveList, target);
}

// Explicit template instantiations
template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);


/// generate<QUIET_CHECKS> generates all pseudo-legal non-captures and knight
/// underpromotions that give check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<QUIET_CHECKS>(const Position& pos, ExtMove* moveList) {
#ifdef ANTI
  if (pos.is_anti())
      return moveList;
#endif
#ifdef RACE
  if (pos.is_race())
      return moveList;
#endif

  assert(!pos.checkers());

  Color us = pos.side_to_move();
  Bitboard dc = pos.discovered_check_candidates();

  while (dc)
  {
     Square from = pop_lsb(&dc);
     PieceType pt = type_of(pos.piece_on(from));

     if (pt == PAWN)
         continue; // Will be generated together with direct checks

     Bitboard b = pos.attacks_from(pt, from) & ~pos.pieces();

     if (pt == KING)
         b &= ~PseudoAttacks[QUEEN][pos.square<KING>(~us)];

     while (b)
         *moveList++ = make_move(from, pop_lsb(&b));
  }

#ifdef ATOMIC
  if (pos.is_atomic())
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<ATOMIC_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<HORDE_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef LOSERS
  if (pos.is_losers())
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<LOSERS_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
#ifdef RELAY
  if (pos.is_relay())
      return us == WHITE ? generate_all<RELAY_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                         : generate_all<RELAY_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, QUIET_CHECKS>(pos, moveList, ~pos.pieces())
                     : generate_all<CHESS_VARIANT, BLACK, QUIET_CHECKS>(pos, moveList, ~pos.pieces());
}


/// generate<EVASIONS> generates all pseudo-legal check evasions when the side
/// to move is in check. Returns a pointer to the end of the move list.
template<>
ExtMove* generate<EVASIONS>(const Position& pos, ExtMove* moveList) {
#ifdef ANTI
  if (pos.is_anti())
      return moveList;
#endif
#ifdef RACE
  if (pos.is_race())
      return moveList;
#endif

  assert(pos.checkers());

  Color us = pos.side_to_move();
  Square ksq = pos.square<KING>(us);
  Bitboard sliderAttacks = 0;
  Bitboard sliders = pos.checkers() & ~pos.pieces(KNIGHT, PAWN);
#ifdef ATOMIC
  Bitboard kingAttacks = pos.is_atomic() ? pos.attacks_from<KING>(pos.square<KING>(~us)) : 0;
#endif

#ifdef ATOMIC
  if (pos.is_atomic())
  {
      // Blasts that explode the opposing king or explode all checkers
      // are counted among evasive moves.
      Bitboard target = pos.pieces(~us), b = pos.checkers();
      while (b)
      {
          Square s = pop_lsb(&b);
          target &= pos.attacks_from<KING>(s) | s;
      }
      target |= kingAttacks;
      target &= pos.pieces(~us) & ~pos.attacks_from<KING>(ksq);
      moveList = (us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, CAPTURES>(pos, moveList, target)
                              : generate_all<ATOMIC_VARIANT, BLACK, CAPTURES>(pos, moveList, target));
  }
#endif

  // Find all the squares attacked by slider checkers. We will remove them from
  // the king evasions in order to skip known illegal moves, which avoids any
  // useless legality checks later on.
  while (sliders)
  {
      Square checksq = pop_lsb(&sliders);
      sliderAttacks |= LineBB[checksq][ksq] ^ checksq;
  }

  // Generate evasions for king, capture and non capture moves
  Bitboard b;
#ifdef ATOMIC
  if (pos.is_atomic()) // Generate evasions for king, non capture moves
      b = pos.attacks_from<KING>(ksq) & ~pos.pieces() & ~(sliderAttacks & ~kingAttacks);
  else
#endif
  b = pos.attacks_from<KING>(ksq) & ~pos.pieces(us) & ~sliderAttacks;
#ifdef LOSERS
  if (pos.is_losers() && pos.can_capture_losers())
      b &= pos.pieces(~us);
#endif
  while (b)
      *moveList++ = make_move(ksq, pop_lsb(&b));

  if (more_than_one(pos.checkers()))
      return moveList; // Double check, only a king move can save the day

  // Generate blocking evasions or captures of the checking piece
  Square checksq = lsb(pos.checkers());
  Bitboard target;
#ifdef ATOMIC
  if (pos.is_atomic()) // Generate blocking evasions of the checking piece
      target = between_bb(checksq, ksq);
  else
#endif
  target = between_bb(checksq, ksq) | checksq;

#ifdef ATOMIC
  if (pos.is_atomic())
      return us == WHITE ? generate_all<ATOMIC_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<ATOMIC_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef CRAZYHOUSE
  if (pos.is_house())
      return us == WHITE ? generate_all<CRAZYHOUSE_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<CRAZYHOUSE_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef HORDE
  if (pos.is_horde())
      return us == WHITE ? generate_all<HORDE_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<HORDE_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
#ifdef LOSERS
  if (pos.is_losers())
  {
      if (pos.can_capture_losers())
          target &= pos.pieces(~us);
      return us == WHITE ? generate_all<LOSERS_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<LOSERS_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
  }
#endif
#ifdef RELAY
  if (pos.is_relay())
      return us == WHITE ? generate_all<RELAY_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                         : generate_all<RELAY_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
#endif
  return us == WHITE ? generate_all<CHESS_VARIANT, WHITE, EVASIONS>(pos, moveList, target)
                     : generate_all<CHESS_VARIANT, BLACK, EVASIONS>(pos, moveList, target);
}


/// generate<LEGAL> generates all the legal moves in the given position

template<>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moveList) {
  // Return immediately at end of variant
  if (pos.is_variant_end())
      return moveList;

  Bitboard pinned = pos.pinned_pieces(pos.side_to_move());
  bool validate = pinned;
#ifdef RACE
  if (pos.is_race()) validate = true;
#endif
  Square ksq = pos.square<KING>(pos.side_to_move());
  ExtMove* cur = moveList;
  moveList = pos.checkers() ? generate<EVASIONS    >(pos, moveList)
                            : generate<NON_EVASIONS>(pos, moveList);
  while (cur != moveList)
      if (   (validate || from_sq(*cur) == ksq || type_of(*cur) == ENPASSANT)
#ifdef CRAZYHOUSE
          && !(pos.is_house() && type_of(*cur) == DROP)
#endif
          && !pos.legal(*cur))
          *cur = (--moveList)->move;
#ifdef ATOMIC
      else if (pos.is_atomic() && pos.capture(*cur) && !pos.legal(*cur))
          *cur = (--moveList)->move;
#endif
      else
          ++cur;

  return moveList;
}
