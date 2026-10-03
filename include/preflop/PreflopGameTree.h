#ifndef TEXASSOLVER_PREFLOPGAMETREE_H
#define TEXASSOLVER_PREFLOPGAMETREE_H

#include <memory>
#include <vector>
#include <atomic>
#include <stdexcept>
#include "include/preflop/PreflopGameTreeNode.h"
#include "include/preflop/PreflopRule.h"

using namespace std;

class TreeTooLargeException : public std::runtime_error {
public:
    int node_count;
    TreeTooLargeException(int count, int limit)
        : std::runtime_error("Game tree exceeded " + to_string(limit) + " nodes (reached " + to_string(count) + ")"),
          node_count(count) {}
};

class PreflopGameTree {
private:
    int num_players;
    vector<float> player_stacks;
    float small_blind;
    float big_blind;
    float ante;

    // Sizing rules
    vector<float> open_sizes;     // RFI sizes (e.g. [2.0f, 2.5f, 3.0f] in big blinds)
    vector<float> raise_sizes;    // 3-bet / 4-bet multipliers (e.g. [3.0f, 4.0f] of the previous bet)
    bool allow_allin;              // Whether to include all-in as an option in the tree

    shared_ptr<PreflopGameTreeNode> root;

    // Node count tracking during build
    int node_count_ = 0;
    int max_nodes_ = 5000000; // 5 million node limit

    shared_ptr<PreflopGameTreeNode> __build(shared_ptr<PreflopGameTreeNode> parent, PreflopRule rule, string path_action = "", int bet_level = 1);

public:
    PreflopGameTree(
        int num_players,
        const vector<float>& player_stacks,
        float small_blind,
        float big_blind,
        float ante,
        const vector<float>& open_sizes,
        const vector<float>& raise_sizes,
        bool allow_allin = true
    );

    // Build the tree and return the root node. Throws TreeTooLargeException if node limit exceeded.
    shared_ptr<PreflopGameTreeNode> build();

    // Set max node limit (default: 5M)
    void set_max_nodes(int max_nodes) { max_nodes_ = max_nodes; }
    int get_node_count() const { return node_count_; }

    shared_ptr<PreflopGameTreeNode> getRoot() const { return root; }
};

#endif // TEXASSOLVER_PREFLOPGAMETREE_H
