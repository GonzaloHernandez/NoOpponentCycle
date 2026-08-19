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

#include "ortools/constraint_solver/constraint_solver.h"
#include "ortools/constraint_solver/constraint_solveri.h"

#ifndef WINNING_CONDITIONS_H
#include "winning_conditions.h"
#endif

namespace ORToolsInt {

// Namespace and type aliases to avoid collisions with Chuffed
namespace cp = operations_research;

using ORIntVar     = cp::IntVar;
using ORSolver     = cp::Solver;
using ORConstraint = cp::Constraint;
using ORDecision   = cp::Decision;
using ORDecBuilder = cp::DecisionBuilder;
using ORDemon      = cp::Demon;

//=============================================================================

class NOCPropagator : public ORConstraint {
private:
    Game&                   g;
    std::vector<ORIntVar*>  V;
    parity_type             playerSAT;
    vec<WinningCondition*>  winConditions;

    const int CF_STAY     = 1;
    const int CF_CONFLICT = 2;
    const int CF_DONE     = 3;

public:
    
    NOCPropagator(ORSolver* s, Game& g, const std::vector<ORIntVar*>& V, 
                  parity_type playerSAT, vec<WinningCondition*>& winConditions)
    : ORConstraint(s), g(g), V(V), playerSAT(playerSAT), winConditions(winConditions) {}

    //-------------------------------------------------------------------------

    void Post() override {
        ORDemon* const d = solver()->MakeConstraintInitialPropagateCallback(this);
        for (size_t i = 0; i < g.nvertices; i++) {
            V[i]->WhenBound(d);
        }
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
    
    int32_t findVertex(int vertex, vec<int32_t>& path) {
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == vertex) return static_cast<int32_t>(i);
        }
        return -1;
    }
    
    //-------------------------------------------------------------------------
    
    void clausify(vec<int32_t>& path, std::vector<ORIntVar*>& lits) {
        for (size_t i = 0; i < path.size() - 1; i++) {
            lits.push_back(V[path[i]]);
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
               int32_t v, int32_t last, int32_t out, bool isFixed) 
    {
        int index = findVertex(v, pathV);
        if (index >= 0) {
            if (!satisfiedConditions(pathV, pathE, index)) {
                if (g.owners[last] == playerSAT) {
                    if (V[last]->Contains(out)) {
                        if (V[last]->Size() == 1) {
                            return CF_CONFLICT;
                        }
                        V[last]->RemoveValue(out);
                    }
                    return CF_STAY;
                } else {
                    if (!V[last]->Contains(-1)) {
                        return CF_CONFLICT;
                    }
                    V[last]->SetValue(-1);
                    return CF_DONE;
                }
            }
        }
        else if (isFixed) {
            pathV.push(v);
            if (g.owners[v] == playerSAT) {
                int64_t i = V[v]->Value();
                int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                int32_t w = g.targets[e];
                if (!V[w]->Bound() || V[w]->Value() >= 0) {
                    pathE.push(e);
                    int status = filter(pathV, pathE, w, v, static_cast<int32_t>(i), 
                                        V[w]->Bound());
                    pathE.pop();
                    if (status != CF_STAY) return status;
                }
            } else {
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                    int32_t w = g.targets[e];
                    if (!V[w]->Bound() || V[w]->Value() >= 0) {
                        pathE.push(e);
                        int status = filter(pathV, pathE, w, v, static_cast<int32_t>(i), 
                                            V[w]->Bound());
                        pathE.pop();
                        if (status != CF_STAY) return status;
                    }
                }
            }
            pathV.pop();
        }
        return CF_STAY;
    }
    
    //-------------------------------------------------------------------------
    
    bool propagate() {
        vec<int32_t> pathV;
        vec<int32_t> pathE;

        if (!V[g.init]->Bound()) return true;

        if (filter(pathV, pathE, g.init, -1, -1, true) == CF_CONFLICT)
            return false;

        return true;
    }
};

//=============================================================================

class NOCBrancher : public ORDecBuilder {
private:
    Game&                   g;
    std::vector<ORIntVar*>  V;
    parity_type             playerSAT;

public:
    
    NOCBrancher(Game& g, const std::vector<ORIntVar*>& V, parity_type playerSAT) 
    : g(g), V(V), playerSAT(playerSAT) {}

    //-------------------------------------------------------------------------
    
    bool finished() {
        for (size_t i = 0; i < V.size(); i++) {
            if (g.owners[i] == playerSAT) {
                if (!V[i]->Bound() && V[i]->Min() >= 0) {
                    return false;
                }
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------

    ORDecision* Next(ORSolver* const s) override {
        for (size_t v = 0; v < V.size(); v++) {
            if (g.owners[v] == playerSAT) {
                if (!V[v]->Bound() && V[v]->Min() >= 0) {
                    int64_t val = V[v]->Min();
                    return s->MakeAssignVariableValue(V[v], val);
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
    vec<WinningCondition*>& winConditions;
    std::string             brancherType;
    int                     printtype;
    parity_type             playerSAT;

public:

    NOCModel(Game& g, vec<WinningCondition*>& winConditions, 
             int printtype = 0, parity_type playerSAT = EVEN, std::string brancherType = "valmin") 
    : solver("NOCModel"), g(g), winConditions(winConditions), printtype(printtype), 
      playerSAT(playerSAT), brancherType(brancherType)
    {
        V.resize(g.nvertices);
        setupConstraints();
    }

    //-------------------------------------------------------------------------

    void setupConstraints() {

        // Creating variables and fixing domains for playerSAT's vertices
        for (size_t v = 0; v < g.nvertices; v++) {
            if (g.owners[v] == playerSAT) {
                V[v] = solver.MakeIntVar(-1, static_cast<int64_t>(g.outs[v].size()) - 1, "V_" + std::to_string(v));
            } else { // opponent vertices
                V[v] = solver.MakeIntVar(-1, 0, "V_" + std::to_string(v));
            }
        }

        // Initial vertex (V_init >= 0)
        solver.AddConstraint(solver.MakeGreaterOrEqual(V[g.init], 0));

        // Connection vertices (V_v -> V_w)
        for (size_t v = 0; v < g.nvertices; v++) {
            if (g.owners[v] == playerSAT) {
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                    int32_t w = g.targets[e];
                    
                    ORIntVar* sel = solver.MakeIsEqualCstVar(V[v], static_cast<int64_t>(i));
                    ORIntVar* geq = solver.MakeIsGreaterOrEqualCstVar(V[w], 0);
                    
                    // sel -> geq  <=>  sel <= geq
                    solver.AddConstraint(solver.MakeLessOrEqual(sel, geq));
                }
            }
            else {
                ORIntVar* sel = solver.MakeIsGreaterOrEqualCstVar(V[v], 0);
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                    int32_t w = g.targets[e];
                    
                    ORIntVar* geq = solver.MakeIsGreaterOrEqualCstVar(V[w], 0);
                    
                    // sel -> geq  <=>  sel <= geq
                    solver.AddConstraint(solver.MakeLessOrEqual(sel, geq));
                }
            }
        }

        // --------------------------------------------------------------------
        // Every infinite OPPONENT play must be avoided regarding condition.
        solver.AddConstraint(solver.RevAlloc(
            new NOCPropagator(&solver, g, V, playerSAT, winConditions)));
    }

    //-------------------------------------------------------------------------

    bool solve() {
        ORDecBuilder* db = nullptr;
        if (brancherType == "reach") {
            db = solver.RevAlloc(new NOCBrancher(g, V, playerSAT));
        } 
        else if (brancherType == "rand") {
            db = solver.MakePhase(V, ORSolver::CHOOSE_RANDOM, ORSolver::ASSIGN_MIN_VALUE);
        }
        else { // if (brancherType == "valmin")
            db = solver.MakePhase(V, ORSolver::CHOOSE_FIRST_UNBOUND, ORSolver::ASSIGN_MIN_VALUE);
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

    void print(std::ostream& out) {
        if (printtype) {
            std::vector<int32_t> vertices;
            std::vector<int32_t> edges;
            for (size_t v = 0; v < V.size(); v++) {
                if (V[v]->Min() < 0) continue;

                vertices.push_back(static_cast<int32_t>(v));
                if (g.owners[v] == playerSAT) {
                    if (V[v]->Bound()) {
                        int32_t e = g.outs[v][static_cast<unsigned int>(V[v]->Value())];
                        edges.push_back(e);
                    }
                } else {
                    for (size_t i = 0; i < g.outs[v].size(); i++) {
                        int32_t e = g.outs[v][static_cast<unsigned int>(i)];
                        edges.push_back(e);
                    }
                }
            }

            out << "V=[";
            for (size_t v = 0; v < vertices.size(); v++) {
                out << (v ? "," : "") << vertices[v];
            }
            out << "]\nE=[";
            for (size_t e = 0; e < edges.size(); e++) {
                out << (e ? "," : "") << edges[e];
            }
            out << "]\n";
        }
    }
};

} // namespace ORToolsInt