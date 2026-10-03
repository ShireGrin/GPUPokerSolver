#include "include/preflop/PreflopGameTree.h"
#include <algorithm>
#include <sstream>

PreflopGameTree::PreflopGameTree(
    int num_players,
    const vector<float>& player_stacks,
    float small_blind,
    float big_blind,
    float ante,
    const vector<float>& open_sizes,
    const vector<float>& raise_sizes,
    bool allow_allin
) {
    this->num_players = num_players;
    this->player_stacks = player_stacks;
    this->small_blind = small_blind;
    this->big_blind = big_blind;
    this->ante = ante;
    this->open_sizes = open_sizes;
    this->raise_sizes = raise_sizes;
    this->allow_allin = allow_allin;
}

shared_ptr<PreflopGameTreeNode> PreflopGameTree::build() {
    node_count_ = 0;
    PreflopRule initial_rule(num_players, player_stacks, small_blind, big_blind, ante);
    root = __build(nullptr, initial_rule, "ROOT", 1);
    return root;
}

shared_ptr<PreflopGameTreeNode> PreflopGameTree::__build(
    shared_ptr<PreflopGameTreeNode> parent,
    PreflopRule rule,
    string path_action,
    int bet_level
) {
    node_count_++;
    if (node_count_ > max_nodes_) {
        throw TreeTooLargeException(node_count_, max_nodes_);
    }

    if (rule.is_terminal()) {
        int winner = rule.get_winner();
        if (winner != -1) {
            return make_shared<PreflopTerminalNode>(parent, rule, winner, path_action);
        } else {
            return make_shared<PreflopShowdownNode>(parent, rule, path_action);
        }
    }

    int active_p = rule.current_player;
    auto action_node = make_shared<PreflopActionNode>(parent, rule, active_p, path_action);

    // 1. FOLD
    if (rule.get_call_amount() > 0.0f) {
        PreflopRule fold_rule = rule;
        fold_rule.apply_fold();
        auto fold_child = __build(action_node, fold_rule, "FOLD", bet_level);
        action_node->add_child("FOLD", fold_child);
    }

    // 2. CALL / CHECK
    PreflopRule call_rule = rule;
    float call_amt = call_rule.get_call_amount();
    string call_name = (call_amt == 0.0f) ? "CHECK" : "CALL";
    call_rule.apply_call();
    auto call_child = __build(action_node, call_rule, call_name, bet_level);
    action_node->add_child(call_name, call_child);

    // 3. RAISE / ALL-IN
    float max_commit = rule.get_max_commit();
    float current_commit = rule.player_commits[active_p];
    float remaining_stack = rule.get_remaining_stack(active_p);

    if (remaining_stack > 0.0f) {
        vector<float> possible_raises;

        if (bet_level == 1) {
            // First in / Open raise (2-bet)
            for (float sz : open_sizes) {
                float target = sz * big_blind;
                if (target > max_commit && target - current_commit <= remaining_stack) {
                    possible_raises.push_back(target);
                }
            }
        } else if (bet_level == 2) {
            // 3-bet
            if (!raise_sizes.empty()) {
                float mult = raise_sizes[0];
                float target = max_commit * mult;
                if (target > max_commit && target - current_commit <= remaining_stack) {
                    possible_raises.push_back(target);
                }
            }
        } else if (bet_level == 3) {
            // 4-bet
            if (raise_sizes.size() >= 2) {
                float mult = raise_sizes[1];
                float target = max_commit * mult;
                if (target > max_commit && target - current_commit <= remaining_stack) {
                    possible_raises.push_back(target);
                }
            }
        }

        // Add All-in as an option (if enabled)
        if (allow_allin) {
            float allin_target = current_commit + remaining_stack;
            // Only allow all-in open shoves if short-stacked (<= 30 BB);
            // deep stacks can only shove if facing a 3-bet or higher (bet_level >= 3).
            bool should_allow_shove = (player_stacks[active_p] <= 30.0f) || (bet_level >= 3);
            // For 7+ players, allow shoves more freely to keep strategy options available
            if (num_players >= 7) should_allow_shove = true;
            if (should_allow_shove) {
                if (std::find(possible_raises.begin(), possible_raises.end(), allin_target) == possible_raises.end()) {
                    possible_raises.push_back(allin_target);
                }
            }
        }

        // Deduplicate and sort raises
        std::sort(possible_raises.begin(), possible_raises.end());
        possible_raises.erase(std::unique(possible_raises.begin(), possible_raises.end()), possible_raises.end());

        for (float target : possible_raises) {
            PreflopRule raise_rule = rule;
            raise_rule.apply_raise(target);
            
            // Format raise name
            stringstream ss;
            float allin_target = current_commit + remaining_stack;
            if (target == allin_target) {
                ss << "ALLIN";
            } else {
                ss << "RAISE " << (int)target;
            }
            string raise_name = ss.str();

            auto raise_child = __build(action_node, raise_rule, raise_name, bet_level + 1);
            action_node->add_child(raise_name, raise_child);
        }
    }

    return action_node;
}
