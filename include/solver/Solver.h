//
// Created by Xuefeng Huang on 2020/1/31.
//

#ifndef TEXASSOLVER_SOLVER_H
#define TEXASSOLVER_SOLVER_H


#include <include/GameTree.h>

class Solver {
public:
    enum MonteCarolAlg {
        NONE,
        PUBLIC
    };
    Solver();
    Solver(shared_ptr<GameTree> tree);
    shared_ptr<GameTree> getTree();
    virtual void train() = 0;
    virtual void stop() = 0;
    virtual json dumps(bool with_status,int depth) = 0;
    virtual vector<vector<vector<float>>> get_strategy(const shared_ptr<ActionNode>& node,vector<Card> cards) = 0;
    virtual vector<vector<vector<float>>> get_evs(const shared_ptr<ActionNode>& node,vector<Card> cards) = 0;
    virtual shared_ptr<Trainable> get_trainable(const shared_ptr<ActionNode>& node,vector<Card> chance_cards) = 0;
    
    virtual json dumps_strategy() { return json(); }
    virtual void loads_strategy_and_evs(const shared_ptr<GameTreeNode>& node, const json& data) {}

    virtual bool save_solve_to_file(const string& filepath, const string& config_str, const string& metadata_json_str) { return false; }
    virtual bool load_solve_from_file(const string& filepath) { return false; }
    static bool read_config_from_file(const string& filepath, string& config_str, string& metadata_str);

    shared_ptr<GameTree> tree;
    float last_exploitability = -1.0f;
    int last_iteration = -1;
};


#endif //TEXASSOLVER_SOLVER_H
