/*
 * Main authors:
 *    Gonzalo Hernandez <gonzalo.hernandez@monash>
 *    <gonzalo.hernandez@udenar.edu.co>
 *
 * Contributing authors:
 *    Guido Tack <guido.tack@monash.edu>
 *    Julian Gutierrez <J.Gutierrez@sussex.ac.uk>
 *
 * This file is part of NOCQ (a CP Toolchain for parity games with quantitative
 * conditions).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License,
 * v. 2.0. If a copy of the MPL was not distributed with this file, You can get
 * one at https://mozilla.org/MPL/2.0/.
 *
 *-----------------------------------------------------------------------------
 */
#ifndef WINNING_CONDITIONS_H
#define WINNING_CONDITIONS_H

#ifndef GAME_H
#include "../utils/game.h"
#endif

#include "chuffed/support/vec.h"

struct range {
  int32_t first, last;
};

//=============================================================================

class WinningCondition {
protected:
  Game &g;
  parity_type playerSAT;

public:
  WinningCondition(Game &g, parity_type playerSAT = EVEN)
      : g(g), playerSAT(playerSAT) {}
  virtual ~WinningCondition() = default;
  //-----------------------------------------------------------------------
  virtual bool satisfy(vec<int32_t> &pathV, vec<int32_t> &pathE,
                       int32_t cycleIndex) {
    return true;
  }
  //-----------------------------------------------------------------------
  virtual bool satisfySCC(vec<int32_t> pathV, vec<int32_t> &S) { return true; }
};

//===========================================================================

class ParityCondition : public WinningCondition {
  using WinningCondition::WinningCondition;

public:
  bool satisfy(vec<int32_t> &pathV, vec<int32_t> &pathE,
               int32_t cycleIndex) override {
    int64_t m = g.priors[pathV[cycleIndex]];
    for (int32_t i = cycleIndex + 1; i < pathV.size(); i++) {
      if (g.isBetter(g.priors[pathV[i]], m)) {
        m = g.priors[pathV[i]];
      }
    }
    return m % 2 == playerSAT;
  }
  //-----------------------------------------------------------------------
  bool satisfySCC(vec<int32_t> pathV, vec<int32_t> &S) {
    int64_t m = g.priors[pathV[0]];
    S.push(pathV[0]);
    for (int32_t i = 1; i < pathV.size(); i++) {
      if (g.isBetter(g.priors[pathV[i]], m)) {
        m = g.priors[pathV[i]];
        if (m % 2 == playerSAT) {
          S.clear();
          S.push(pathV[i]);
        }
      } else if (g.priors[pathV[i]] == m) {
        if (m % 2 == playerSAT) {
          S.push(pathV[i]);
        }
      }
    }
    if (m % 2 != playerSAT) {
      S.clear();
      return false;
    }
    return true;
  }
};

//===========================================================================

#endif // WINNING_CONDITIONS_H