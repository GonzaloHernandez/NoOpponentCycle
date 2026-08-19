/*
 * Main authors:
 *    Gonzalo Hernandez <gonzalo.hernandez@monash>
 *    <gonzalo.hernandez@udenar.edu.co>
 *
 * Contributing authors:
 *    Guido Tack <guido.tack@monash.edu>
 *    Julian Gutierrez <J.Gutierrez@sussex.ac.uk>
 *
 * Ported to Google OR-Tools (C++)
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

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <initializer_list>

#include "ortools/constraint_solver/constraint_solver.h"
#include "ortools/constraint_solver/constraint_solveri.h"

#ifndef WINNING_CONDITIONS_H
#include "winning_conditions.h"
#endif

#ifndef TARJAN_H
#include "../utils/tarjan.h"
#endif

namespace ORToolsBool {

namespace cp = operations_research;

using ORIntVar     = cp::IntVar;
using ORSolver     = cp::Solver;
using ORConstraint = cp::Constraint;
using ORDecision   = cp::Decision;
using ORDecBuilder = cp::DecisionBuilder;
using ORDemon      = cp::Demon;

//=============================================================================

class NOCCheckerSCC : public ORConstraint {
private:
    Game&                   g;
    std::vector<ORIntVar*>  V;
    std::vector<ORIntVar*>  E;
    parity_type             playerSAT;
    WinningCondition*       winCondition;

public:
    //-----------------------------------------------------------------------
    NOCCheckerSCC(ORSolver* s, Game& g, const std::vector<ORIntVar*>& V, 
                  const std::vector<ORIntVar*>& E, parity_type playerSAT, 
                  WinningCondition* winCondition) 
    : ORConstraint(s), g(g), V(V), E(E), playerSAT(playerSAT), winCondition(winCondition) {}

    //-----------------------------------------------------------------------
    void Post() override {
        ORDemon* const d = solver()->MakeConstraintInitialPropagateCallback(this);
        for (unsigned int i = 0; i < g.owners.size();  i++) V[i]->WhenBound(d);
        for (unsigned int i = 0; i < g.sources.size(); i++) E[i]->WhenBound(d);
    }

    //-----------------------------------------------------------------------
    void InitialPropagate() override {
        ExecutePropagate();
    }

    //-----------------------------------------------------------------------
    void ExecutePropagate() {
        if (!propagate()) {
            solver()->Fail();
        }
    }

    //-----------------------------------------------------------------------
    bool backtrack() {
        return false;
    }

    //-----------------------------------------------------------------------
    bool check(GameView& view) {
        // Line 1: Tarjan(G)
        TarjanSCC tarjan(g, view);
        vec<vec<int32_t>*> sccs;
        tarjan.solve(sccs);

        bool status = true;

        for (int i = 0; i < sccs.size(); i++) {
            vec<int32_t>& scc = *sccs[i];
            vec<int32_t> objectiveSet;

            // Compute objectiveSet = { v in scc | f_player(scc) holds true }
            winCondition->satisfySCC(scc, objectiveSet);

            // Line 2: IF |scc| == 1
            if (scc.size() == 1) {
                int32_t v = scc[0];

                // Line 4: Check if E_[vEv] holds (Active self loop)
                bool has_active_self_loop = false;
                vec<int32_t> outEdges;
                view.getOuts(outEdges, v);
                for (int j = 0; j < outEdges.size(); j++) {
                    if (g.targets[outEdges[j]] == v) {
                        has_active_self_loop = true;
                        break;
                    }
                }

                // If active self-loop and objectiveSet is empty -> Win Opponent Cycle Found
                if (has_active_self_loop && objectiveSet.size() == 0) {
                    status = false; 
                    break;
                }
            } 
            // Line 8: ELSE (|scc| > 1)
            else {
                // Line 11: IF objectiveSet == empty -> Win Opponent Cycle Found
                if (objectiveSet.size() == 0) {
                    status = false; 
                    break;
                }

                // Line 16: Recursive invocation on G \ objectiveSet
                for (int j = 0; j < objectiveSet.size(); j++) {
                    view.vs[objectiveSet[j]] = false;
                }

                // Deep recursion
                bool subResult = check(view);

                // Backtrack mask to restore view state for peer SCC loops
                for (int j = 0; j < objectiveSet.size(); j++) {
                    view.vs[objectiveSet[j]] = true;
                }

                if (!subResult) {
                    status = false;
                    break;
                }
            }
        }

        // Deallocate memory generated by Tarjan's solver
        for (int i = 0; i < sccs.size(); i++) {
            delete sccs[i];
        }

        return status;
    }

    //-----------------------------------------------------------------------
    bool propagate() {
        GameView view(g);
        view.deactiveAll();

        for (unsigned int i = 0; i < g.nvertices; i++) {
            if (!V[i]->Bound()) return true;
            if (V[i]->Value() == 1) view.vs[i] = true;
        }
        for (unsigned int i = 0; i < g.nedges; i++) {
            if (!E[i]->Bound()) return true;
            if (E[i]->Value() == 1) view.es[i] = true;
        }

        if (!check(view)) {
            return backtrack();
        }

        return true;
    }
};

//=============================================================================

class NOCPropagator : public ORConstraint {
private:
    Game&                   g;
    std::vector<ORIntVar*>  V;
    std::vector<ORIntVar*>  E;
    parity_type             playerSAT;
    vec<WinningCondition*>  winConditions;
    bool                    memo;

    const int CF_STAY     = 1;
    const int CF_CONFLICT = 2;
    const int CF_DONE     = 3;

public:

    NOCPropagator(ORSolver* s, Game& g, const std::vector<ORIntVar*>& V, 
                  const std::vector<ORIntVar*>& E, parity_type playerSAT, 
                  vec<WinningCondition*>& winConditions, bool memo = false)
    : ORConstraint(s), g(g), V(V), E(E), playerSAT(playerSAT), 
      winConditions(winConditions), memo(memo) {}

    //-------------------------------------------------------------------------

    void Post() override {
        ORDemon* const d = solver()->MakeConstraintInitialPropagateCallback(this);
        for (size_t i = 0; i < g.nvertices; i++) V[i]->WhenBound(d);
        for (size_t i = 0; i < g.nedges;    i++) E[i]->WhenBound(d);
    }

    //-------------------------------------------------------------------------

    void InitialPropagate() override {
        ExecutePropagate();
    }

    //-------------------------------------------------------------------------

    void ExecutePropagate() {
        if (!propagate()) {
            solver()->Fail();
        }
    }

    //-------------------------------------------------------------------------
    
    int32_t findVertex(int32_t vertex, vec<int32_t>& path) {
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == vertex) return static_cast<int32_t>(i);
        }
        return -1;
    }
    
    //-------------------------------------------------------------------------
    
    void clausify(vec<int32_t>& path, std::vector<ORIntVar*>& lits) {
        for (size_t i = 0; i < path.size() - 1; i++) {
            lits.push_back(E[path[i]]);
        }
    }
    
    //-------------------------------------------------------------------------
    
    bool satisfiedConditions(vec<int32_t>& pathV, vec<int32_t>& pathE, int32_t index) {
        if (playerSAT == EVEN) {
            for (size_t i = 0; i < winConditions.size(); i++) {
                if (!winConditions[i]->satisfy(pathV, pathE, index)) {
                    return false;
                }
            }
            return true;
        }
        else {
            for (size_t i = 0; i < winConditions.size(); i++) {
                if (winConditions[i]->satisfy(pathV, pathE, index)) {
                    return true;
                }
            }
            return false;
        }
    }

    //-------------------------------------------------------------------------
    
    int filter(vec<int32_t>& pathV, vec<int32_t>& pathE,
               int32_t v, int32_t lastEdge, bool definedEdge) 
    {
        int32_t index = findVertex(v, pathV);
        if (index >= 0) {
            if (!satisfiedConditions(pathV, pathE, index)) {
                if (lastEdge >= 0) {
                    if (!E[lastEdge]->Contains(0)) {
                        return CF_CONFLICT;
                    }
                    E[lastEdge]->SetValue(0);
                }
            }
        }
        else if (definedEdge) {
            pathV.push(v);
            for (size_t i = 0; i < g.outs[v].size(); i++) {
                int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                if (E[e]->Bound() && E[e]->Value() == 0) continue;

                int w = g.targets[e];
                pathE.push(e);
                int status = filter(pathV, pathE, w, e, E[e]->Bound() && E[e]->Value() == 1);
                pathE.pop();
                if (status == CF_CONFLICT) {
                    return status;
                }
            }
            pathV.pop();
        }
        return CF_STAY;
    }

    //-------------------------------------------------------------------------

    enum DFSColor { WHITE = 0, GRAY = 1, BLACK = 2 };

    int filterMemo(vec<int32_t>& pathV, vec<int32_t>& pathE,
                   int32_t v, int32_t lastEdge, bool definedEdge, vec<DFSColor>& flags)
    {
        if (flags[v] == BLACK) {
            return CF_STAY;
        }

        if (flags[v] == GRAY) {
            int32_t index = findVertex(v, pathV);
            if (!satisfiedConditions(pathV, pathE, index)) {
                if (lastEdge >= 0) {
                    if (!E[lastEdge]->Contains(0)) {
                        return CF_CONFLICT;
                    }
                    E[lastEdge]->SetValue(0);
                }
            }
            return CF_STAY;
        }

        // flags[v] == WHITE
        if (definedEdge) {
            flags[v] = GRAY;
            pathV.push(v);

            for (size_t i = 0; i < g.outs[v].size(); i++) {
                int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                if (E[e]->Bound() && E[e]->Value() == 0) continue;

                int w = g.targets[e];
                pathE.push(e);
                int status = filterMemo(pathV, pathE, 
                                        w, e, E[e]->Bound() && E[e]->Value() == 1, flags);
                pathE.pop();

                if (status == CF_CONFLICT) {
                    pathV.pop();
                    flags[v] = WHITE;
                    return status;
                }
            }

            pathV.pop();
            flags[v] = BLACK;
        }

        return CF_STAY;
    }

    //-------------------------------------------------------------------------
    
    bool propagate() {
        vec<int32_t> pathV;
        vec<int32_t> pathE;

        if (!memo) {
            if (filter(pathV, pathE, g.init, -1, true) == CF_CONFLICT)
                return false;
        } else {
            vec<DFSColor> flags(g.nvertices, WHITE);
            if (filterMemo(pathV, pathE, g.init, -1, true, flags) == CF_CONFLICT)
                return false;
        }

        return true;
    }
};

//=============================================================================

class NOCBrancher : public ORDecBuilder {
private:
    Game&                   g;
    std::vector<ORIntVar*>  V;
    std::vector<ORIntVar*>  E;
    parity_type             playerSAT;

public:
    
    NOCBrancher(Game& g, const std::vector<ORIntVar*>& V, 
                const std::vector<ORIntVar*>& E, parity_type playerSAT) 
    : g(g), V(V), E(E), playerSAT(playerSAT) {}

    //-------------------------------------------------------------------------
    
    bool finished() {
        for (size_t v = 0; v < V.size(); v++) {
            if (g.owners[v] == playerSAT && V[v]->Bound() && V[v]->Value() == 1) {
                for (size_t j = 0; j < g.outs[v].size(); j++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(j)];
                    if (!E[e]->Bound()) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------

    ORDecision* Next(ORSolver* const s) override {
        for (size_t v = 0; v < V.size(); v++) {
            if (g.owners[v] == playerSAT && V[v]->Bound() && V[v]->Value() == 1) {
                for (size_t j = 0; j < g.outs[v].size(); j++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(j)];
                    if (!E[e]->Bound()) {
                        return s->MakeAssignVariableValue(E[e], 1);
                    }
                }
            }
        }
        return nullptr;
    }
};

//=============================================================================

class NOCModel {
private:
    ORSolver                solver;
    Game&                   g;
    std::vector<ORIntVar*>  V;
    std::vector<ORIntVar*>  E;
    vec<WinningCondition*>& qlWinConditions;
    std::string             brancherType;
    bool                    propEager, propMemo, propChecker;
    int                     printtype;
    parity_type             playerSAT;

public:

    //-------------------------------------------------------------------------

    NOCModel(Game& g,
        vec<WinningCondition*>& qlWinConditions,
        int printtype = 0, parity_type playerSAT = EVEN, std::string brancherType = "valmin",
        bool propEager = true, bool propMemo = false, bool propChecker = false) 
    : solver("NOCModelBool"),
      g(g), 
      qlWinConditions(qlWinConditions),
      printtype(printtype), 
      playerSAT(playerSAT), brancherType(brancherType), 
      propEager(propEager), propMemo(propMemo), propChecker(propChecker)
    {
        V.resize(g.nvertices);
        E.resize(g.nedges);
        setupConstraints();
    }

    //-------------------------------------------------------------------------

    void setupConstraints() {

        for (size_t i = 0; i < g.nvertices; i++) V[i] = solver.MakeBoolVar("V_" + std::to_string(i));
        for (size_t i = 0; i < g.nedges;    i++) E[i] = solver.MakeBoolVar("E_" + std::to_string(i));

        // Initial vertex
        fixVertices({g.init}, {});

        // --------------------------------------------------------------------
        // For every active PLAYER vertex, one outgoing edge must be activated
        for (int32_t v = 0; v < g.nvertices; v++) if (g.owners[v] == playerSAT) {

            int32_t n = g.outs[v].size();
            if (n == 0) continue;

            std::vector<ORIntVar*> out_vars;
            out_vars.reserve(n);
            for (size_t i = 0; i < g.outs[v].size(); i++) {
                int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                out_vars.push_back(E[e]);
            }

            // --- At least one : sum(E_out) >= V[v]
            solver.AddConstraint(solver.MakeGreaterOrEqual(solver.MakeSum(out_vars), V[v]));

            // --- At most one : sum(E_out) <= 1
            if (n > 1) {
                solver.AddConstraint(solver.MakeLessOrEqual(solver.MakeSum(out_vars), 1));
            }
        }

        // --------------------------------------------------------------------
        // Every active OPPONENT vertex, each outgoing edge must be activated
        for (size_t v = 0; v < g.nvertices; v++) {
            if (g.owners[v] == opponent(playerSAT)) {
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                    // V[v] -> E[e] <=> V[v] <= E[e]
                    solver.AddConstraint(solver.MakeLessOrEqual(V[v], E[e]));
                }
            }
        }

        // --------------------------------------------------------------------
        // For every active edge, the target vertex must be activated
        for (size_t w = 0; w < g.nvertices; w++) if (w != g.init) {
            for (size_t i = 0; i < g.ins[w].size(); i++) {
                int32_t e = g.ins[w][static_cast<unsigned int>(i)];
                // E[e] -> V[w] <=> E[e] <= V[w]
                solver.AddConstraint(solver.MakeLessOrEqual(E[e], V[w]));
            }
        }

        // --------------------------------------------------------------------
        // Every infinite OPPONENT play must be avoided regarding condition.

        if (propEager) {
            if (qlWinConditions.size() > 0) 
                solver.AddConstraint(solver.RevAlloc(
                    new NOCPropagator(&solver, g, V, E, playerSAT, qlWinConditions)));
        } 
        if (propMemo) {
            if (qlWinConditions.size() > 0) 
                solver.AddConstraint(solver.RevAlloc(
                    new NOCPropagator(&solver, g, V, E, playerSAT, qlWinConditions, true)));
        }
        if (propChecker) {
            if (qlWinConditions.size() > 0)
                solver.AddConstraint(solver.RevAlloc(
                    new NOCCheckerSCC(&solver, g, V, E, playerSAT, qlWinConditions[0])));
        }

        //---------------------------------------------------------------------
    }

    //-------------------------------------------------------------------------

    bool solve() {
        std::vector<ORIntVar*> all_vars;
        all_vars.reserve(V.size() + E.size());
        all_vars.insert(all_vars.end(), V.begin(), V.end());
        all_vars.insert(all_vars.end(), E.begin(), E.end());

        ORDecBuilder* db = nullptr;
        if (brancherType == "reach") {
            db = solver.RevAlloc(new NOCBrancher(g, V, E, playerSAT));
        } 
        else if (brancherType == "rand") {
            db = solver.MakePhase(all_vars, ORSolver::CHOOSE_RANDOM, ORSolver::ASSIGN_MIN_VALUE);
        } 
        else { // if (brancherType == "valmin") 
            db = solver.MakePhase(all_vars, ORSolver::CHOOSE_FIRST_UNBOUND, ORSolver::ASSIGN_MIN_VALUE);
        }

        solver.NewSearch(db);
        bool found = solver.NextSolution();
        if (found) {
            print(std::cout);
        }
        solver.EndSearch();
        return found;
    }

    //-------------------------------------------------------------------------

    void fixVertices(std::initializer_list<int32_t> vs,
                     std::initializer_list<int32_t> nvs = {})
    {
        for (int32_t v : vs) {
            solver.AddConstraint(solver.MakeEquality(V[v], 1));
        }
        for (int32_t v : nvs) {
            solver.AddConstraint(solver.MakeEquality(V[v], 0));
        }
    }

    //-------------------------------------------------------------------------

    void fixEdges(std::initializer_list<int32_t> es,
                  std::initializer_list<int32_t> nes = {}) 
    {
        for (int32_t e : es) {
            solver.AddConstraint(solver.MakeEquality(E[e], 1));
        }
        for (int32_t e : nes) {
            solver.AddConstraint(solver.MakeEquality(E[e], 0));
        }
    }

    //-------------------------------------------------------------------------

    void print(std::ostream& out) {
        if (printtype) {
            out << "V=[";
            bool first = true;
            for (size_t i = 0; i < V.size(); i++) {
                if (V[i]->Bound() && V[i]->Value() == 1) {
                    if (first) first = false; else out << ",";
                    out << i;
                }
            }
            out << "]\nE=[";
            first = true;
            for (size_t i = 0; i < E.size(); i++) {
                if (E[i]->Bound() && E[i]->Value() == 1) {
                    if (first) first = false; else out << ",";
                    out << i;
                }
            }
            out << "]\n";
        }
    }
};

} // namespace ORToolsBool