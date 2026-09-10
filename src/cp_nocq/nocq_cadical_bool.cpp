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

#include <iostream>
#include "cadical.hpp"
#include <queue>
#include <unordered_map>

#ifndef WINNING_CONDITIONS_H
#include "winning_conditions.h"
#endif

namespace CaDiCaL {

#define BoolSAT int
#define Lit int

class NOCPropagator : public ExternalPropagator {
private:
    Game& g;
    vec<BoolSAT>&       V;
    vec<vec<BoolSAT>>&  E;
    vec<int>            assigns;
    parity_type         playerSAT;
    vec<WinningCondition*> winConditions;

    int current_reason_lit;
    size_t reasonLit;

    std::queue<int> propQueue;
    std::unordered_map<int, vec<int>> propReasons;

    vec<vec<int>> trail;
    vec<int32_t>      vertex_pos;

    int getValLit(int v) { return isFixed(v) ? assigns[v] * v : 0; }
    bool isFixed(int v)  { return v >= 0 && v < (int)assigns.size() && assigns[v] != 0; }
    bool isFalse(int v)  { return v >= 0 && v < (int)assigns.size() && assigns[v] == -1; }
    bool isTrue(int v)   { return v >= 0 && v < (int)assigns.size() && assigns[v] == 1; }

public:
    //-------------------------------------------------------------------------
    NOCPropagator(Game& game, vec<BoolSAT>& V, vec<vec<BoolSAT>>& E, 
        parity_type playerSAT, vec<WinningCondition*> winConditions, int max_var)
    : g(game), V(V), E(E), playerSAT(playerSAT), winConditions(winConditions), 
      current_reason_lit(0), reasonLit(0), assigns(max_var + 1, 0), 
      vertex_pos(g.nvertices, -1)
    {
        trail.push();
    }

    ~NOCPropagator() override = default;

    //-------------------------------------------------------------------------
    void notify_assignment(const std::vector<int> &lits) override {
        for (int lit : lits) {
            int v = abs(lit);
            if (v >= (int)assigns.size()) {
                assigns.growTo(v + 1, 0);
            }
            if (assigns[v] != 0) continue;
            assigns[v] = (lit > 0) ? 1 : -1;
            trail.last().push(v);
        }
    }

    //-------------------------------------------------------------------------
    void notify_new_decision_level() override {
        trail.push();
    }

    //-------------------------------------------------------------------------
    void notify_backtrack(size_t new_level) override {
        while (trail.size() > (new_level + 1)) {
            for (int i = 0; i < trail.last().size(); i++) { 
                int v = trail.last()[i];
                if (v < (int)assigns.size()) {
                    assigns[v] = 0;
                }
            }
            trail.pop();
        }
    }

    //-------------------------------------------------------------------------
    bool cb_check_found_model(const std::vector<int> &model) override {
        return true;
    }

    //-------------------------------------------------------------------------
    int cb_decide() override {
        return 0;
    }

    //-------------------------------------------------------------------------
    int cb_propagate() override {
        if (!propQueue.empty()) {
            int lit = propQueue.front();
            propQueue.pop();
            return lit;
        }

        vec<int32_t> pathV;
        vec<int32_t> pathE;

        filter(pathV, pathE, g.init, -1, true);

        if (!propQueue.empty()) {
            int lit = propQueue.front();
            propQueue.pop();
            return lit;
        }

        return 0;
    }

    //-------------------------------------------------------------------------
    int cb_add_reason_clause_lit(int propagated_lit) override {
        if (current_reason_lit != propagated_lit) {
            current_reason_lit = propagated_lit;
            reasonLit = 0;
        }

        auto it = propReasons.find(propagated_lit);
        if (it == propReasons.end()) return 0;

        const vec<int>& reason = it->second;
        if (reasonLit < (size_t)reason.size()) {
            return reason[reasonLit++];
        }
        current_reason_lit = 0;
        reasonLit = 0;
        return 0;
    }

    //-------------------------------------------------------------------------
    bool cb_has_external_clause(bool &is_forgettable) override {
        return false;
    }

    //-------------------------------------------------------------------------
    int cb_add_external_clause_lit() override {
        return 0;
    }

    //-------------------------------------------------------------------------
    int findVertex(int vertex, const vec<int32_t>& path) {
        for (int i = 0; i < path.size(); i++) {
            if (path[i] == vertex) return i;
        }
        return -1;
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
        } else {
            for (size_t i = 0; i < winConditions.size(); i++) {
                if (winConditions[i]->satisfy(pathV, pathE, index)) {
                    return true;
                }
            }
            return false;
        }
    }

    //-------------------------------------------------------------------------
    bool filter(vec<int32_t>& pathV, vec<int32_t>& pathE,
                int32_t v, int32_t last, bool is_fixed) 
    {
        int index = vertex_pos[v];
        if (index >= 0) {
            if (!satisfiedConditions(pathV, pathE, index)) {
                int32_t novar = (last >= 0 && g.owners[last] == playerSAT) 
                                ? E[last][pathE.last()] 
                                : V[v];
                
                int propLit = -novar;
                propReasons[propLit].clear();
                
                // Explaining clause: (propLit \lor \bigvee \neg Antecedents)
                propReasons[propLit].push(propLit);

                for (int i = 0; i < pathE.size(); i++) {
                    int u = pathV[i];
                    propReasons[propLit].push(-V[u]);
                    if (g.owners[u] == playerSAT) {
                        propReasons[propLit].push(-E[u][pathE[i]]);
                    }
                }
                
                propQueue.push(propLit);
                return isTrue(novar);
            }
            return false;
        }

        if (is_fixed) {
            vertex_pos[v] = pathV.size();
            pathV.push(v);
            for (size_t i = 0; i < g.outs[v].size(); i++) {
                int32_t e = g.outs[v][i];
                int32_t w = g.targets[e];
                
                if (isFalse(V[w])) continue;
                if (g.owners[v] == playerSAT && isFalse(E[v][i])) continue;

                bool next_fixed = (g.owners[v] == playerSAT) ? isTrue(E[v][i]) : true;

                pathE.push(i);
                bool confl = filter(pathV, pathE, w, v, next_fixed);
                pathE.pop();
                if (confl) {
                    vertex_pos[v] = -1;
                    return true;
                }
            }
            pathV.pop();
            vertex_pos[v] = -1;
        }
        return false;
    }
};

//=============================================================================

class NOCModel {
private:
    Game& g;
    vec<BoolSAT>        V;
    vec<vec<BoolSAT>>   E;
    vec<WinningCondition*> winConditions;
    parity_type playerSAT;

    int pool = 0;
    Solver* solver;
    NOCPropagator* noc;

    int newBoolVar() { pool += 1; return pool; }

public:
    //-------------------------------------------------------------------------
    NOCModel(Game& g, vec<WinningCondition*>& winConditions, parity_type playerSAT = EVEN)
    : g(g), winConditions(winConditions), playerSAT(playerSAT), noc(nullptr) 
    {
        solver = new Solver();
        // solver->set("factor", 0);

        solver->configure("plain");
        solver->set("walk", 0);
        solver->set("rephase", 0);
        solver->set("stabilize", 0); // Pure VSIDS like MiniSat

        // Fixed size: E must be indexed by vertices
        V.growTo(g.nvertices, 0);
        E.growTo(g.nvertices);

        for (int v = 0; v < g.nvertices; v++) {
            V[v] = newBoolVar();
            if (g.owners[v] == playerSAT) {
                E[v].growTo(g.outs[v].size(), 0);
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    E[v][i] = newBoolVar();
                }
            }
        }

        setupConstraints();

        for (int v = 0; v < g.nvertices; v++) {
            solver->add_observed_var(V[v]);
            if (g.owners[v] == playerSAT) {
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    solver->add_observed_var(E[v][i]);
                }
            }
        }
    }

    //-------------------------------------------------------------------------
    void setupConstraints() {
        solver->clause(V[g.init]);

        for (int v = 0; v < g.nvertices; v++) {
            if (g.owners[v] == playerSAT) {
                // At least one edge
                solver->add(-V[v]);
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    solver->add(E[v][i]);
                }
                solver->add(0);

                // At most one edge (prevent hyper-strategies)
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    for (size_t j = i + 1; j < g.outs[v].size(); j++) {
                        solver->clause(-E[v][i], -E[v][j]);
                    }
                }

                // Edge targets
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][i];
                    int32_t w = g.targets[e];
                    solver->clause(-E[v][i], V[w]);
                }
            } else {
                // Opponent targets
                for (size_t i = 0; i < g.outs[v].size(); i++) {
                    int32_t e = g.outs[v][i];
                    int32_t w = g.targets[e];
                    solver->clause(-V[v], V[w]);
                }
            }
        }

        // Pass total pool count to safely size internal tables
        noc = new NOCPropagator(g, V, E, playerSAT, winConditions, pool);
        solver->connect_external_propagator(noc);
    }

    ~NOCModel() {
        if (solver && noc) {
            solver->disconnect_external_propagator();
        }
        delete noc;
        delete solver;
    }

    //-------------------------------------------------------------------------
    bool solve() {
        return solver->solve() == 10;
    }

    //-------------------------------------------------------------------------
    void print() {
        std::cout << "V=[";
        bool first = true;
        for (int i = 0; i < V.size(); i++) {
            if (solver->val(V[i]) == 1) {
                if (first) first = false; else std::cout << ",";
                std::cout << i;
            }
        }
        std::cout << "]" << std::endl;
    }

    //-------------------------------------------------------------------------
    void statistics() {
        solver->statistics();
    }
};

} // namespace CaDiCaL
