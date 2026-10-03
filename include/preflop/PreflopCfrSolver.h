#ifndef TEXASSOLVER_PREFLOPCFRSOLVER_H
#define TEXASSOLVER_PREFLOPCFRSOLVER_H

#include "include/solver/Solver.h"
#include "include/preflop/PreflopGameTreeNode.h"
#include "include/preflop/PreflopIcmCalculator.h"
#include "include/preflop/PreflopEquityManager.h"
#include "include/ranges/PrivateCards.h"
#include "include/compairer/Compairer.h"
#include <memory>
#include <vector>
#include <map>
#include <atomic>
#include <functional>

#ifdef USE_LIBTORCH
#include "include/preflop/NeuralNetEvaluator.h"
#endif

using namespace std;

class PreflopCfrSolver {
private:
    shared_ptr<PreflopGameTreeNode> root;
    vector<vector<PrivateCards>> player_ranges;
    PreflopIcmCalculator icm_calc;
    shared_ptr<PreflopEquityManager> equity_manager;
    shared_ptr<Compairer> hand_evaluator;

    int num_players;
    int iteration_number;
    bool debug;
    int print_interval;
    int num_threads;

    // Fast hand index mapping: hand_to_index[player][card1 * 52 + card2]
    vector<vector<int>> hand_to_index;

    // 2-way precomputed equity cache: flat 1D vector of size 2704 * 2704
    vector<float> two_way_equity_cache;

    // Stop solving flag
    std::atomic<bool> nowstop{false};

#ifdef USE_LIBTORCH
    std::shared_ptr<NeuralNetEvaluator> nn_evaluator;
    bool use_neural_net = false;
#endif

    // Progress callback: (iteration, exploitability, avg_duration_ms, first_duration_ms, last_duration_ms) -> void
    std::function<void(int, float, float, float, float)> progress_callback;

    void initialize_all_trainables(const shared_ptr<PreflopGameTreeNode>& node);
    void update_current_strategies(const shared_ptr<PreflopGameTreeNode>& node, vector<vector<float>> reach_probs);

public:
    float last_exploitability = -1.0f;
    int last_iteration = -1;

    PreflopCfrSolver(
        shared_ptr<PreflopGameTreeNode> root,
        const vector<vector<PrivateCards>>& player_ranges,
        const PreflopIcmCalculator& icm_calc,
        shared_ptr<PreflopEquityManager> equity_manager,
        shared_ptr<Compairer> hand_evaluator,
        int iteration_number = 5000,
        bool debug = false,
        int print_interval = 10,
        int num_threads = -1
    );

    virtual ~PreflopCfrSolver() = default;

    void train();
    void stop();
    void set_progress_callback(std::function<void(int, float, float, float, float)> cb) { progress_callback = cb; }
#ifdef USE_LIBTORCH
    void setNeuralNetEvaluator(std::shared_ptr<NeuralNetEvaluator> evaluator) {
        nn_evaluator = evaluator;
        use_neural_net = (evaluator != nullptr && evaluator->isLoaded());
    }
#endif
    float calculate_exploitability();

    // Count total nodes in the tree
    static int count_nodes(const shared_ptr<PreflopGameTreeNode>& node);

    json dumps_strategy(const shared_ptr<PreflopGameTreeNode>& node);
    json dumps_evs(const shared_ptr<PreflopGameTreeNode>& node);
    json dumps_solve(const shared_ptr<PreflopGameTreeNode>& node);
    void loads_strategy_and_evs(const shared_ptr<PreflopGameTreeNode>& node, const json& data);

    shared_ptr<PreflopGameTreeNode> getRoot() const { return root; }

    vector<float> evaluate_average_evs(
        int target_player,
        const shared_ptr<PreflopGameTreeNode>& node,
        const vector<vector<float>>& reach_probs
    );

    vector<float> evaluate_best_response_evs(
        int target_player,
        const shared_ptr<PreflopGameTreeNode>& node,
        const vector<vector<float>>& reach_probs
    );

    vector<float> cfr(
        int target_player,
        const shared_ptr<PreflopGameTreeNode>& node,
        const vector<vector<float>>& reach_probs,
        int iter
    );

    vector<float> actionUtility(
        int target_player,
        const shared_ptr<PreflopActionNode>& node,
        const vector<vector<float>>& reach_probs,
        int iter
    );

    vector<float> showdownUtility(
        int target_player,
        const shared_ptr<PreflopShowdownNode>& node,
        const vector<vector<float>>& reach_probs,
        int iter
    );

    vector<float> terminalUtility(
        int target_player,
        const shared_ptr<PreflopTerminalNode>& node,
        const vector<vector<float>>& reach_probs,
        int iter
    );

    // Get exact 2-way equity between two hands
    float get_2way_equity(const PrivateCards& h1, const PrivateCards& h2);

    // Setup helper matrices
    void setup_hand_to_index();
    void precompute_active_2way_equities();
};

#endif // TEXASSOLVER_PREFLOPCFRSOLVER_H
