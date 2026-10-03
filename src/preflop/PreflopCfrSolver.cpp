#include "include/preflop/PreflopCfrSolver.h"
#include "include/preflop/PreflopTrainable.h"
#ifdef USE_LIBTORCH
#include "include/preflop/NeuralNetEvaluator.h"
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
#include <iostream>
#include <algorithm>
#include <random>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

PreflopCfrSolver::PreflopCfrSolver(
    shared_ptr<PreflopGameTreeNode> root,
    const vector<vector<PrivateCards>>& player_ranges,
    const PreflopIcmCalculator& icm_calc,
    shared_ptr<PreflopEquityManager> equity_manager,
    shared_ptr<Compairer> hand_evaluator,
    int iteration_number,
    bool debug,
    int print_interval,
    int num_threads
) {
    this->root = root;
    this->player_ranges = player_ranges;
    this->icm_calc = icm_calc;
    this->equity_manager = equity_manager;
    this->hand_evaluator = hand_evaluator;
    this->num_players = player_ranges.size();
    this->iteration_number = iteration_number;
    this->debug = debug;
    this->print_interval = print_interval;
    this->num_threads = num_threads;
}

void PreflopCfrSolver::train() {
    int threads_to_use = num_threads;
    if (threads_to_use <= 0) {
        threads_to_use = 12; // default to 12 threads
    }
#ifdef _OPENMP
    omp_set_num_threads(threads_to_use);
#endif
    if (debug) {
        std::cout << "Preflop solver running with " << threads_to_use << " threads." << std::endl;
    }

    initialize_all_trainables(root);
    setup_hand_to_index();
    
    bool skip_equity_precompute = false;
#ifdef USE_LIBTORCH
    skip_equity_precompute = use_neural_net;
#endif
    if (two_way_equity_cache.empty()) {
        two_way_equity_cache = std::vector<float>(2704 * 2704, -1.0f);
    }
    if (!skip_equity_precompute) {
        precompute_active_2way_equities();
    }

    int print_int = print_interval;
    if (print_int <= 0) print_int = 10;

    float first_duration_ms = 0.0f;
    float last_duration_ms = 0.0f;
    float total_duration_ms = 0.0f;

    std::cout << "DEBUG: PreflopCfrSolver::train() starting loop. nowstop=" << (nowstop ? "true" : "false") << ", iteration_number=" << iteration_number << std::endl;
    for (int iter = 0; iter < iteration_number; ++iter) {
        if (nowstop) {
            std::cout << "DEBUG: PreflopCfrSolver::train() loop broken because nowstop is true." << std::endl;
            break;
        }
        std::cout << "DEBUG: Starting iteration " << iter << std::endl;
        this->last_iteration = iter;

        auto iter_start = std::chrono::steady_clock::now();

        // In preflop CFR, reach probabilities start at each player's private cards weight
        vector<vector<float>> reach_probs(num_players);
        for (int p = 0; p < num_players; ++p) {
            reach_probs[p] = vector<float>(player_ranges[p].size());
            for (size_t i = 0; i < player_ranges[p].size(); ++i) {
                reach_probs[p][i] = player_ranges[p][i].weight;
            }
        }

        // Pre-compute current strategies for all active nodes to make CFR traversal read-only and thread-safe
        update_current_strategies(root, reach_probs);

        // Run traversal for each target player in parallel using OpenMP
        #pragma omp parallel for
        for (int p = 0; p < num_players; ++p) {
            cfr(p, root, reach_probs, iter);
        }
        std::cout << "DEBUG: Completed traversal for iteration " << iter << std::endl;

        auto iter_end = std::chrono::steady_clock::now();
        float duration_ms = std::chrono::duration<float, std::milli>(iter_end - iter_start).count();

        if (iter == 0) {
            first_duration_ms = duration_ms;
        }
        last_duration_ms = duration_ms;
        total_duration_ms += duration_ms;

        if (iter > 0 && (iter % print_int == 0 || iter == iteration_number - 1)) {
            last_exploitability = calculate_exploitability();
            float avg_duration_ms = total_duration_ms / (iter + 1);
            if (progress_callback) {
                progress_callback(iter, last_exploitability, avg_duration_ms, first_duration_ms, last_duration_ms);
            }
            if (debug) {
                cout << "Iteration " << iter << " completed. Exploitability: " << last_exploitability
                     << " mBB/hand | Durations (ms): Last=" << last_duration_ms
                     << ", Avg=" << avg_duration_ms
                     << ", First=" << first_duration_ms << endl;
            }
        }
    }
}

void PreflopCfrSolver::stop() {
    nowstop = true;
}

json PreflopCfrSolver::dumps_strategy(const shared_ptr<PreflopGameTreeNode>& node) {
    if (!node) return json();

    json ret;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        ret["node_type"] = "action_node";
        ret["player"] = action_node->getPlayer();
        
        json children_json = json::object();
        const auto& actions = action_node->getActions();
        const auto& children = action_node->getChildren();
        for (size_t i = 0; i < actions.size(); ++i) {
            children_json[actions[i]] = dumps_strategy(children[i]);
        }
        ret["children"] = children_json;

        shared_ptr<Trainable> trainable = action_node->getTrainable();
        if (trainable) {
            ret["strategy"] = trainable->dump_strategy(false);
        }
    } else if (node->getType() == PreflopGameTreeNode::NodeType::SHOWDOWN) {
        ret["node_type"] = "showdown_node";
    } else if (node->getType() == PreflopGameTreeNode::NodeType::TERMINAL) {
        auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
        ret["node_type"] = "terminal_node";
        ret["winner"] = terminal_node->getWinner();
    }
    return ret;
}

json PreflopCfrSolver::dumps_evs(const shared_ptr<PreflopGameTreeNode>& node) {
    if (!node) return json();

    json ret;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        ret["node_type"] = "action_node";
        ret["player"] = action_node->getPlayer();
        
        json children_json = json::object();
        const auto& actions = action_node->getActions();
        const auto& children = action_node->getChildren();
        for (size_t i = 0; i < actions.size(); ++i) {
            children_json[actions[i]] = dumps_evs(children[i]);
        }
        ret["children"] = children_json;

        shared_ptr<Trainable> trainable = action_node->getTrainable();
        if (trainable) {
            ret["evs"] = trainable->dump_evs();
        }
    } else if (node->getType() == PreflopGameTreeNode::NodeType::SHOWDOWN) {
        ret["node_type"] = "showdown_node";
    } else if (node->getType() == PreflopGameTreeNode::NodeType::TERMINAL) {
        auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
        ret["node_type"] = "terminal_node";
        ret["winner"] = terminal_node->getWinner();
    }
    return ret;
}

vector<float> PreflopCfrSolver::cfr(
    int target_player,
    const shared_ptr<PreflopGameTreeNode>& node,
    const vector<vector<float>>& reach_probs,
    int iter
) {
    if (nowstop) return vector<float>();

    switch (node->getType()) {
        case PreflopGameTreeNode::NodeType::ACTION: {
            auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
            return actionUtility(target_player, action_node, reach_probs, iter);
        }
        case PreflopGameTreeNode::NodeType::SHOWDOWN: {
            auto showdown_node = dynamic_pointer_cast<PreflopShowdownNode>(node);
            return showdownUtility(target_player, showdown_node, reach_probs, iter);
        }
        case PreflopGameTreeNode::NodeType::TERMINAL: {
            auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
            return terminalUtility(target_player, terminal_node, reach_probs, iter);
        }
        default:
            throw runtime_error("unknown preflop node type");
    }
}

vector<float> PreflopCfrSolver::actionUtility(
    int target_player,
    const shared_ptr<PreflopActionNode>& node,
    const vector<vector<float>>& reach_probs,
    int iter
) {
    int acting_player = node->getPlayer();
    const vector<PrivateCards>& node_player_private_cards = player_ranges[acting_player];
    int acting_hand_num = node_player_private_cards.size();
    
    int target_hand_num = player_ranges[target_player].size();
    vector<float> payoffs(target_hand_num, 0.0f);

    const vector<string>& actions = node->getActions();
    const vector<shared_ptr<PreflopGameTreeNode>>& children = node->getChildren();

    shared_ptr<Trainable> trainable = node->getTrainable();
    if (!trainable) {
        trainable = make_shared<PreflopTrainable>(&player_ranges[acting_player], actions.size());
        node->setTrainable(trainable);
    }

    const vector<float>& current_strategy = trainable->getcurrentStrategy();

    vector<float> regrets(actions.size() * acting_hand_num, 0.0f);
    vector<vector<float>> all_action_utility(actions.size());

    for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
        vector<vector<float>> new_reach_probs = reach_probs;
        if (acting_player == target_player) {
            all_action_utility[action_id] = cfr(target_player, children[action_id], reach_probs, iter);
        } else {
            for (int hand_id = 0; hand_id < acting_hand_num; ++hand_id) {
                float strategy_prob = current_strategy[action_id * acting_hand_num + hand_id];
                new_reach_probs[acting_player][hand_id] = reach_probs[acting_player][hand_id] * strategy_prob;
            }
            all_action_utility[action_id] = cfr(target_player, children[action_id], new_reach_probs, iter);
        }
    }

    if (nowstop) return vector<float>();

    for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
        const auto& action_utilities = all_action_utility[action_id];
        if (action_utilities.empty()) continue;

        for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
            if (target_player == acting_player) {
                float strategy_prob = current_strategy[action_id * target_hand_num + hand_id];
                payoffs[hand_id] += strategy_prob * action_utilities[hand_id];
            } else {
                payoffs[hand_id] += action_utilities[hand_id];
            }
        }
    }

    if (target_player == acting_player) {
        for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
            for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                if (all_action_utility[action_id].empty()) continue;
                regrets[action_id * target_hand_num + hand_id] =
                    all_action_utility[action_id][hand_id] - payoffs[hand_id];
            }
        }
        trainable->updateRegrets(regrets, iter + 1, reach_probs[target_player]);

        if (iter % print_interval == 0 || iter == iteration_number - 1) {
            vector<float> evs(actions.size() * target_hand_num, 0.0f);
            std::vector<float> opp_sums(num_players, 0.0f);
            std::vector<std::vector<float>> opp_card_sums(num_players, std::vector<float>(52, 0.0f));
            for (int p = 0; p < num_players; ++p) {
                if (p == target_player) continue;
                for (size_t i = 0; i < player_ranges[p].size(); ++i) {
                    const auto& pc = player_ranges[p][i];
                    float prob = reach_probs[p][i];
                    opp_sums[p] += prob;
                    opp_card_sums[p][pc.card1] += prob;
                    opp_card_sums[p][pc.card2] += prob;
                }
            }

            for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                if (all_action_utility[action_id].empty()) continue;
                for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
                    const auto& pc_target = player_ranges[target_player][hand_id];
                    int d1 = pc_target.card1;
                    int d2 = pc_target.card2;

                    float weight = 1.0f;
                    for (int p = 0; p < num_players; ++p) {
                        if (p == target_player) continue;
                        int idx = hand_to_index[p][std::min(d1, d2) * 52 + std::max(d1, d2)];
                        float pi_j = (idx != -1) ? reach_probs[p][idx] : 0.0f;
                        float S_j = opp_sums[p] - opp_card_sums[p][d1] - opp_card_sums[p][d2] + pi_j;
                        weight *= S_j;
                    }
                    if (weight <= 0.0f) weight = 1.0f;
                    
                    evs[action_id * target_hand_num + hand_id] = all_action_utility[action_id][hand_id] / weight;
                }
            }
            trainable->setEv(evs);
        }
    }

    return payoffs;
}

vector<float> PreflopCfrSolver::showdownUtility(
    int target_player,
    const shared_ptr<PreflopShowdownNode>& node,
    const vector<vector<float>>& reach_probs,
    int iter
) {
    (void)iter;
    const PreflopRule& rule = node->getRule();
    int target_hand_num = player_ranges[target_player].size();
    vector<float> payoffs(target_hand_num, 0.0f);

#ifdef USE_LIBTORCH
    auto showdown_node = dynamic_pointer_cast<PreflopShowdownNode>(node);
    if (use_neural_net && showdown_node->has_nn_evs) {
        // --- Neural Net path ---
        auto combo_index = [](int c1, int c2) -> int {
            int lo = std::min(c1, c2);
            int hi = std::max(c1, c2);
            return lo * 52 - lo * (lo + 1) / 2 + (hi - lo - 1);
        };

        // Determine which players are OOP (lower index) and IP (higher index)
        std::vector<int> active_players_vec;
        for (int p = 0; p < num_players; ++p) {
            if (rule.active_players[p]) {
                active_players_vec.push_back(p);
            }
        }

        int oop_player = active_players_vec[0];
        int ip_player = active_players_vec[1];
        
        float pot_val = rule.get_pot();

        // Map NN EVs back to payoffs for the target_player
        // The NN returns EVs in chips. We need to convert to ICM payouts.
        bool is_target_oop = (target_player == oop_player);
        const std::vector<float>& target_nn_evs = is_target_oop ? showdown_node->nn_oop_evs : showdown_node->nn_ip_evs;

            // Compute card-removal-weighted payoffs
            int opp_player = is_target_oop ? ip_player : oop_player;
            std::vector<float> opp_sums(num_players, 0.0f);
            std::vector<std::vector<float>> opp_card_sums(num_players, std::vector<float>(52, 0.0f));
            for (int p = 0; p < num_players; ++p) {
                if (p == target_player) continue;
                for (size_t i = 0; i < player_ranges[p].size(); ++i) {
                    const auto& pc = player_ranges[p][i];
                    float prob = reach_probs[p][i];
                    opp_sums[p] += prob;
                    opp_card_sums[p][pc.card1] += prob;
                    opp_card_sums[p][pc.card2] += prob;
                }
            }

            for (int i = 0; i < target_hand_num; ++i) {
                const auto& pc_target = player_ranges[target_player][i];
                int d1 = pc_target.card1;
                int d2 = pc_target.card2;
                int ci = combo_index(d1, d2);

                // Card removal weight
                float weight = 1.0f;
                for (int p = 0; p < num_players; ++p) {
                    if (p == target_player) continue;
                    int idx = hand_to_index[p][std::min(d1, d2) * 52 + std::max(d1, d2)];
                    float pi_j = (idx != -1) ? reach_probs[p][idx] : 0.0f;
                    float S_j = opp_sums[p] - opp_card_sums[p][d1] - opp_card_sums[p][d2] + pi_j;
                    weight *= S_j;
                }

                // NN EV for this hand, weighted by opponent reach
                float nn_ev = target_nn_evs[ci];

                // Build final stacks for ICM
                std::vector<float> final_stacks(num_players);
                for (int p = 0; p < num_players; ++p) {
                    final_stacks[p] = rule.get_remaining_stack(p);
                }
                // The NN ev represents the target player's share of the pot
                final_stacks[target_player] += nn_ev;
                // Distribute the rest of the pot to the opponent
                float opp_ev = pot_val - nn_ev;
                if (opp_ev < 0.0f) opp_ev = 0.0f;
                final_stacks[opp_player] += opp_ev;

                std::vector<float> payout_vector = icm_calc.calculate_equities(final_stacks);
                payoffs[i] = payout_vector[target_player] * weight;
            }

            return payoffs;
    }
#endif // USE_LIBTORCH

    vector<int> active_players;
    for (int p = 0; p < num_players; ++p) {
        if (rule.active_players[p]) {
            active_players.push_back(p);
        }
    }

    int num_active = active_players.size();
    if (num_active == 0) return payoffs;
    if (num_active == 1) {
        int winner = active_players[0];
        vector<float> final_stacks(num_players);
        for (int p = 0; p < num_players; ++p) {
            if (p == winner) {
                final_stacks[p] = rule.get_remaining_stack(p) + rule.get_pot();
            } else {
                final_stacks[p] = rule.get_remaining_stack(p);
            }
        }
        vector<float> payout_vector = icm_calc.calculate_equities(final_stacks);
        float target_payout = payout_vector[target_player];

        vector<float> sums(num_players, 0.0f);
        vector<vector<float>> card_sums(num_players, vector<float>(52, 0.0f));
        for (int p = 0; p < num_players; ++p) {
            if (p == target_player) continue;
            for (size_t i = 0; i < player_ranges[p].size(); ++i) {
                const PrivateCards& pc = player_ranges[p][i];
                float prob = reach_probs[p][i];
                sums[p] += prob;
                card_sums[p][pc.card1] += prob;
                card_sums[p][pc.card2] += prob;
            }
        }

        for (int i = 0; i < target_hand_num; ++i) {
            const PrivateCards& pc_target = player_ranges[target_player][i];
            int d1 = pc_target.card1;
            int d2 = pc_target.card2;

            float weight = 1.0f;
            for (int p = 0; p < num_players; ++p) {
                if (p == target_player) continue;
                int idx = hand_to_index[p][min(d1, d2) * 52 + max(d1, d2)];
                float pi_j = (idx != -1) ? reach_probs[p][idx] : 0.0f;
                float S_j = sums[p] - card_sums[p][d1] - card_sums[p][d2] + pi_j;
                weight *= S_j;
            }
            payoffs[i] = target_payout * weight;
        }
        return payoffs;
    }

    vector<float> sums(num_players, 0.0f);
    vector<vector<float>> card_sums(num_players, vector<float>(52, 0.0f));
    for (int p = 0; p < num_players; ++p) {
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            const PrivateCards& pc = player_ranges[p][i];
            float prob = reach_probs[p][i];
            sums[p] += prob;
            card_sums[p][pc.card1] += prob;
            card_sums[p][pc.card2] += prob;
        }
    }

    vector<vector<float>> hand_equities(num_players);
    vector<float> range_avg_equities(num_players, 0.0f);

    for (int p : active_players) {
        int p_hand_num = player_ranges[p].size();
        hand_equities[p] = vector<float>(p_hand_num, 0.0f);
        float total_weight = 0.0f;

        for (int i = 0; i < p_hand_num; ++i) {
            const PrivateCards& pc_p = player_ranges[p][i];
            int d1 = pc_p.card1;
            int d2 = pc_p.card2;

            float sum_opp_eq = 0.0f;
            int num_opps = 0;

            for (int opp : active_players) {
                if (opp == p) continue;
                num_opps++;

                float weighted_eq_sum = 0.0f;
                float opp_non_collide_sum = 0.0f;

                int opp_hand_num = player_ranges[opp].size();
                for (int k = 0; k < opp_hand_num; ++k) {
                    const PrivateCards& pc_opp = player_ranges[opp][k];
                    if (d1 == pc_opp.card1 || d1 == pc_opp.card2 || d2 == pc_opp.card1 || d2 == pc_opp.card2) {
                        continue;
                    }
                    float prob = reach_probs[opp][k];
                    float eq = get_2way_equity(pc_p, pc_opp);
                    weighted_eq_sum += prob * eq;
                    opp_non_collide_sum += prob;
                }

                if (opp_non_collide_sum > 0.0f) {
                    sum_opp_eq += (weighted_eq_sum / opp_non_collide_sum);
                } else {
                    sum_opp_eq += 0.5f;
                }
            }

            float avg_eq = (num_opps > 0) ? (sum_opp_eq / num_opps) : 0.5f;
            
            // Apply EQR (Equity Realization) adjustment
            string pos = "OOP_DEFAULT";
            if (p == num_players - 1) pos = "BB";
            else if (p == num_players - 2 && num_players >= 2) pos = "SB";
            else if (p == num_players - 3 && num_players >= 3) pos = "BTN";

            float eqr_val = equity_manager->get_eqr_factor(pos, pc_p);

            avg_eq *= eqr_val;
            hand_equities[p][i] = avg_eq;
            
            float prob = reach_probs[p][i];
            range_avg_equities[p] += prob * avg_eq;
            total_weight += prob;
        }

        if (total_weight > 0.0f) {
            range_avg_equities[p] /= total_weight;
        } else {
            range_avg_equities[p] = 1.0f / num_active;
        }
    }

    vector<float> unnormalized_range_avg_equities = range_avg_equities;

    float sum_avg = 0.0f;
    for (int p : active_players) {
        sum_avg += range_avg_equities[p];
    }
    if (sum_avg > 0.0f) {
        for (int p : active_players) {
            range_avg_equities[p] /= sum_avg;
        }
    } else {
        for (int p : active_players) {
            range_avg_equities[p] = 1.0f / num_active;
        }
    }

    vector<float> base_final_stacks(num_players);
    for (int p = 0; p < num_players; ++p) {
        if (p == target_player) continue;
        if (rule.active_players[p]) {
            base_final_stacks[p] = rule.get_remaining_stack(p) + range_avg_equities[p] * rule.get_pot();
        } else {
            base_final_stacks[p] = rule.get_remaining_stack(p);
        }
    }

    bool is_target_active = rule.active_players[target_player];

    for (int i = 0; i < target_hand_num; ++i) {
        const PrivateCards& pc_target = player_ranges[target_player][i];
        int d1 = pc_target.card1;
        int d2 = pc_target.card2;

        float weight = 1.0f;
        for (int p = 0; p < num_players; ++p) {
            if (p == target_player) continue;
            int idx = hand_to_index[p][min(d1, d2) * 52 + max(d1, d2)];
            float pi_j = (idx != -1) ? reach_probs[p][idx] : 0.0f;
            float S_j = sums[p] - card_sums[p][d1] - card_sums[p][d2] + pi_j;
            weight *= S_j;
        }

        float norm_eq = 0.5f;
        vector<float> final_stacks = base_final_stacks;
        if (is_target_active) {
            float eq_h = hand_equities[target_player][i];
            if (sum_avg > 0.0f) {
                norm_eq = eq_h / sum_avg;
            } else {
                norm_eq = 1.0f / num_active;
            }
            final_stacks[target_player] = rule.get_remaining_stack(target_player) + norm_eq * rule.get_pot();

            float expected_opp_chips = 0.0f;
            for (int opp : active_players) {
                if (opp == target_player) continue;
                expected_opp_chips += range_avg_equities[opp] * rule.get_pot();
            }
            float actual_opp_chips = (1.0f - norm_eq) * rule.get_pot();
            float scaling_factor = (expected_opp_chips > 0.0f) ? (actual_opp_chips / expected_opp_chips) : 1.0f;
            for (int opp : active_players) {
                if (opp == target_player) continue;
                final_stacks[opp] = rule.get_remaining_stack(opp) + range_avg_equities[opp] * rule.get_pot() * scaling_factor;
            }
        } else {
            final_stacks[target_player] = rule.get_remaining_stack(target_player);
        }

        vector<float> payout_vector = icm_calc.calculate_equities(final_stacks);
        payoffs[i] = payout_vector[target_player] * weight;


    }

    return payoffs;
}

vector<float> PreflopCfrSolver::terminalUtility(
    int target_player,
    const shared_ptr<PreflopTerminalNode>& node,
    const vector<vector<float>>& reach_probs,
    int iter
) {
    (void)iter;
    const PreflopRule& rule = node->getRule();
    int winner = node->getWinner();
    
    int target_hand_num = player_ranges[target_player].size();
    vector<float> payoffs(target_hand_num, 0.0f);

    vector<float> final_stacks(num_players);
    for (int p = 0; p < num_players; ++p) {
        if (p == winner) {
            final_stacks[p] = rule.get_remaining_stack(p) + rule.get_pot();
        } else {
            final_stacks[p] = rule.get_remaining_stack(p);
        }
    }
    vector<float> payout_vector = icm_calc.calculate_equities(final_stacks);
    float target_payout = payout_vector[target_player];

    vector<float> sums(num_players, 0.0f);
    vector<vector<float>> card_sums(num_players, vector<float>(52, 0.0f));
    for (int p = 0; p < num_players; ++p) {
        if (p == target_player) continue;
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            const PrivateCards& pc = player_ranges[p][i];
            float prob = reach_probs[p][i];
            sums[p] += prob;
            card_sums[p][pc.card1] += prob;
            card_sums[p][pc.card2] += prob;
        }
    }

    for (int i = 0; i < target_hand_num; ++i) {
        const PrivateCards& pc_target = player_ranges[target_player][i];
        int d1 = pc_target.card1;
        int d2 = pc_target.card2;

        float weight = 1.0f;
        for (int p = 0; p < num_players; ++p) {
            if (p == target_player) continue;
            int idx = hand_to_index[p][min(d1, d2) * 52 + max(d1, d2)];
            float pi_j = (idx != -1) ? reach_probs[p][idx] : 0.0f;
            float S_j = sums[p] - card_sums[p][d1] - card_sums[p][d2] + pi_j;
            weight *= S_j;
        }

        payoffs[i] = target_payout * weight;


    }

    return payoffs;
}

float PreflopCfrSolver::get_2way_equity(const PrivateCards& h1, const PrivateCards& h2) {
    int idx1 = min(h1.card1, h1.card2) * 52 + max(h1.card1, h1.card2);
    int idx2 = min(h2.card1, h2.card2) * 52 + max(h2.card1, h2.card2);

    int cache_idx1 = idx1 * 2704 + idx2;
    int cache_idx2 = idx2 * 2704 + idx1;

    if (two_way_equity_cache[cache_idx1] >= 0.0f) {
        return two_way_equity_cache[cache_idx1];
    }

    if (h1.card1 == h2.card1 || h1.card1 == h2.card2 || h1.card2 == h2.card1 || h1.card2 == h2.card2) {
        two_way_equity_cache[cache_idx1] = 0.0f;
        two_way_equity_cache[cache_idx2] = 0.0f;
        return 0.0f;
    }

    int trials = 1000;
    float wins = 0.0f;

    vector<int> deck;
    deck.reserve(48);
    for (int i = 0; i < 52; ++i) {
        if (i != h1.card1 && i != h1.card2 && i != h2.card1 && i != h2.card2) {
            deck.push_back(i);
        }
    }

    static thread_local std::random_device rd;
    static thread_local std::mt19937 g(rd());

    for (int t = 0; t < trials; ++t) {
        vector<int> deal_board;
        deal_board.reserve(5);
        for (int i = 0; i < 5; ++i) {
            int r = i + g() % (deck.size() - i);
            swap(deck[i], deck[r]);
            deal_board.push_back(deck[i]);
        }

        vector<int> hand1 = {h1.card1, h1.card2};
        vector<int> hand2 = {h2.card1, h2.card2};

        int rank1 = hand_evaluator->get_rank(hand1, deal_board);
        int rank2 = hand_evaluator->get_rank(hand2, deal_board);

        if (rank1 < rank2) {
            wins += 1.0f;
        } else if (rank1 == rank2) {
            wins += 0.5f;
        }
    }

    float eq = wins / trials;
    two_way_equity_cache[cache_idx1] = eq;
    two_way_equity_cache[cache_idx2] = 1.0f - eq;

    return eq;
}

void PreflopCfrSolver::setup_hand_to_index() {
    num_players = player_ranges.size();
    hand_to_index = vector<vector<int>>(num_players, vector<int>(52 * 52, -1));
    for (int p = 0; p < num_players; ++p) {
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            const PrivateCards& pc = player_ranges[p][i];
            int c1 = min(pc.card1, pc.card2);
            int c2 = max(pc.card1, pc.card2);
            hand_to_index[p][c1 * 52 + c2] = i;
        }
    }
}

void PreflopCfrSolver::precompute_active_2way_equities() {
    two_way_equity_cache = vector<float>(2704 * 2704, -1.0f);
    
    std::string cache_path = "cache/preflop_equity.bin";
    if (!fs::exists(cache_path) && fs::exists("../cache/preflop_equity.bin")) {
        cache_path = "../cache/preflop_equity.bin";
    }
    if (fs::exists(cache_path)) {
        std::ifstream in(cache_path, std::ios::binary);
        if (in) {
            in.read(reinterpret_cast<char*>(two_way_equity_cache.data()), two_way_equity_cache.size() * sizeof(float));
        }
    }

    // Collect all unique hand pairs to avoid duplicate evaluations and data races
    vector<pair<PrivateCards, PrivateCards>> unique_pairs;
    vector<bool> visited(2704 * 2704, false);

    for (int p1 = 0; p1 < num_players; ++p1) {
        for (const auto& h1 : player_ranges[p1]) {
            int idx1 = min(h1.card1, h1.card2) * 52 + max(h1.card1, h1.card2);
            for (int p2 = p1 + 1; p2 < num_players; ++p2) {
                for (const auto& h2 : player_ranges[p2]) {
                    int idx2 = min(h2.card1, h2.card2) * 52 + max(h2.card1, h2.card2);
                    int cache_idx = min(idx1, idx2) * 2704 + max(idx1, idx2);
                    if (two_way_equity_cache[cache_idx] < 0.0f && !visited[cache_idx]) {
                        visited[cache_idx] = true;
                        unique_pairs.push_back({h1, h2});
                    }
                }
            }
        }
    }

    if (!unique_pairs.empty()) {
        // Parallel precomputation of unique pairs using OpenMP
        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < unique_pairs.size(); ++i) {
            get_2way_equity(unique_pairs[i].first, unique_pairs[i].second);
        }

        // Save updated cache to disk
        try {
            fs::create_directories(fs::path(cache_path).parent_path());
            std::ofstream out(cache_path, std::ios::binary);
            if (out) {
                out.write(reinterpret_cast<const char*>(two_way_equity_cache.data()), two_way_equity_cache.size() * sizeof(float));
            }
        } catch (...) {
            // Ignore any save failures silently so it doesn't crash the solver
        }
    }
}

int PreflopCfrSolver::count_nodes(const shared_ptr<PreflopGameTreeNode>& node) {
    if (!node) return 0;
    int count = 1;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        for (const auto& child : action_node->getChildren()) {
            count += count_nodes(child);
        }
    }
    return count;
}

float PreflopCfrSolver::calculate_exploitability() {
    // Initial reach probs (hand weights)
    vector<vector<float>> initial_reach_probs(num_players);
    for (int p = 0; p < num_players; ++p) {
        initial_reach_probs[p] = vector<float>(player_ranges[p].size());
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            initial_reach_probs[p][i] = player_ranges[p][i].weight;
        }
    }

    float total_exploitability = 0.0f;

    // Calculate sums and card_sums for all players at the root
    vector<float> sums(num_players, 0.0f);
    vector<vector<float>> card_sums(num_players, vector<float>(52, 0.0f));
    for (int p = 0; p < num_players; ++p) {
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            const PrivateCards& pc = player_ranges[p][i];
            float prob = initial_reach_probs[p][i];
            sums[p] += prob;
            card_sums[p][pc.card1] += prob;
            card_sums[p][pc.card2] += prob;
        }
    }

    for (int p = 0; p < num_players; ++p) {
        vector<float> avg_evs = evaluate_average_evs(p, root, initial_reach_probs);
        vector<float> br_evs = evaluate_best_response_evs(p, root, initial_reach_probs);

        float sum_avg_ev = 0.0f;
        float sum_br_ev = 0.0f;

        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            float weight = player_ranges[p][i].weight;
            sum_avg_ev += weight * avg_evs[i];
            sum_br_ev += weight * br_evs[i];
        }

        // Calculate total_range_weight to normalize
        float total_range_weight = 0.0f;
        for (size_t i = 0; i < player_ranges[p].size(); ++i) {
            const PrivateCards& pc_target = player_ranges[p][i];
            int d1 = pc_target.card1;
            int d2 = pc_target.card2;

            float weight = 1.0f;
            for (int opp = 0; opp < num_players; ++opp) {
                if (opp == p) continue;
                int idx = hand_to_index[opp][min(d1, d2) * 52 + max(d1, d2)];
                float pi_j = (idx != -1) ? initial_reach_probs[opp][idx] : 0.0f;
                float S_j = sums[opp] - card_sums[opp][d1] - card_sums[opp][d2] + pi_j;
                weight *= S_j;
            }
            total_range_weight += pc_target.weight * weight;
        }

        if (total_range_weight > 0.0f) {
            float normalized_avg_ev = sum_avg_ev / total_range_weight;
            float normalized_br_ev = sum_br_ev / total_range_weight;
            float diff = normalized_br_ev - normalized_avg_ev;
            if (diff > 0.0f) {
                total_exploitability += diff;
            }
        }
    }

    const auto& payouts = icm_calc.get_payouts();
    if (payouts.empty()) {
        // Chip EV Mode: EVs are in BB. Return mBB/hand (milli-big-blinds per hand).
        return (total_exploitability / num_players) * 1000.0f;
    } else {
        // ICM Mode: exploitability is in prize pool units. Return milli-units per hand.
        return (total_exploitability / num_players) * 1000.0f;
    }
}

vector<float> PreflopCfrSolver::evaluate_average_evs(
    int target_player,
    const shared_ptr<PreflopGameTreeNode>& node,
    const vector<vector<float>>& reach_probs
) {
    switch (node->getType()) {
        case PreflopGameTreeNode::NodeType::ACTION: {
            auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
            int acting_player = action_node->getPlayer();
            int acting_hand_num = player_ranges[acting_player].size();
            int target_hand_num = player_ranges[target_player].size();
            vector<float> payoffs(target_hand_num, 0.0f);

            const vector<string>& actions = action_node->getActions();
            const vector<shared_ptr<PreflopGameTreeNode>>& children = action_node->getChildren();

            shared_ptr<Trainable> trainable = action_node->getTrainable();
            vector<float> avg_strategy;
            if (trainable) {
                avg_strategy = trainable->getAverageStrategy();
            } else {
                avg_strategy = vector<float>(actions.size() * acting_hand_num, 1.0f / actions.size());
            }

            vector<vector<float>> all_action_utility(actions.size());
            for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                vector<vector<float>> new_reach_probs = reach_probs;
                if (acting_player == target_player) {
                    all_action_utility[action_id] = evaluate_average_evs(target_player, children[action_id], reach_probs);
                } else {
                    for (int hand_id = 0; hand_id < acting_hand_num; ++hand_id) {
                        float strategy_prob = avg_strategy[action_id * acting_hand_num + hand_id];
                        new_reach_probs[acting_player][hand_id] = reach_probs[acting_player][hand_id] * strategy_prob;
                    }
                    all_action_utility[action_id] = evaluate_average_evs(target_player, children[action_id], new_reach_probs);
                }
            }

            for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                const auto& action_utilities = all_action_utility[action_id];
                if (action_utilities.empty()) continue;

                for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
                    if (target_player == acting_player) {
                        float strategy_prob = avg_strategy[action_id * target_hand_num + hand_id];
                        payoffs[hand_id] += strategy_prob * action_utilities[hand_id];
                    } else {
                        payoffs[hand_id] += action_utilities[hand_id];
                    }
                }
            }
            return payoffs;
        }
        case PreflopGameTreeNode::NodeType::SHOWDOWN: {
            auto showdown_node = dynamic_pointer_cast<PreflopShowdownNode>(node);
            return showdownUtility(target_player, showdown_node, reach_probs, 0);
        }
        case PreflopGameTreeNode::NodeType::TERMINAL: {
            auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
            return terminalUtility(target_player, terminal_node, reach_probs, 0);
        }
        default:
            return vector<float>();
    }
}

vector<float> PreflopCfrSolver::evaluate_best_response_evs(
    int target_player,
    const shared_ptr<PreflopGameTreeNode>& node,
    const vector<vector<float>>& reach_probs
) {
    switch (node->getType()) {
        case PreflopGameTreeNode::NodeType::ACTION: {
            auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
            int acting_player = action_node->getPlayer();
            int acting_hand_num = player_ranges[acting_player].size();
            int target_hand_num = player_ranges[target_player].size();
            vector<float> payoffs(target_hand_num, 0.0f);

            const vector<string>& actions = action_node->getActions();
            const vector<shared_ptr<PreflopGameTreeNode>>& children = action_node->getChildren();

            shared_ptr<Trainable> trainable = action_node->getTrainable();
            vector<float> avg_strategy;
            if (trainable) {
                avg_strategy = trainable->getAverageStrategy();
            } else {
                avg_strategy = vector<float>(actions.size() * acting_hand_num, 1.0f / actions.size());
            }

            vector<vector<float>> all_action_utility(actions.size());
            for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                vector<vector<float>> new_reach_probs = reach_probs;
                if (acting_player == target_player) {
                    all_action_utility[action_id] = evaluate_best_response_evs(target_player, children[action_id], reach_probs);
                } else {
                    for (int hand_id = 0; hand_id < acting_hand_num; ++hand_id) {
                        float strategy_prob = avg_strategy[action_id * acting_hand_num + hand_id];
                        new_reach_probs[acting_player][hand_id] = reach_probs[acting_player][hand_id] * strategy_prob;
                    }
                    all_action_utility[action_id] = evaluate_best_response_evs(target_player, children[action_id], new_reach_probs);
                }
            }

            if (target_player == acting_player) {
                for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
                    float max_ev = -999999.0f;
                    for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                        if (all_action_utility[action_id].empty()) continue;
                        max_ev = max(max_ev, all_action_utility[action_id][hand_id]);
                    }
                    payoffs[hand_id] = (max_ev > -999998.0f) ? max_ev : 0.0f;
                }
            } else {
                for (size_t action_id = 0; action_id < actions.size(); ++action_id) {
                    const auto& action_utilities = all_action_utility[action_id];
                    if (action_utilities.empty()) continue;
                    for (int hand_id = 0; hand_id < target_hand_num; ++hand_id) {
                        payoffs[hand_id] += action_utilities[hand_id];
                    }
                }
            }
            return payoffs;
        }
        case PreflopGameTreeNode::NodeType::SHOWDOWN: {
            auto showdown_node = dynamic_pointer_cast<PreflopShowdownNode>(node);
            return showdownUtility(target_player, showdown_node, reach_probs, 0);
        }
        case PreflopGameTreeNode::NodeType::TERMINAL: {
            auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
            return terminalUtility(target_player, terminal_node, reach_probs, 0);
        }
        default:
            return vector<float>();
    }
}

json PreflopCfrSolver::dumps_solve(const shared_ptr<PreflopGameTreeNode>& node) {
    if (!node) return json();

    json ret;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        ret["node_type"] = "action_node";
        ret["player"] = action_node->getPlayer();
        
        json children_json = json::object();
        const auto& actions = action_node->getActions();
        const auto& children = action_node->getChildren();
        for (size_t i = 0; i < actions.size(); ++i) {
            children_json[actions[i]] = dumps_solve(children[i]);
        }
        ret["children"] = children_json;

        shared_ptr<Trainable> trainable = action_node->getTrainable();
        if (trainable) {
            ret["strategy"] = trainable->dump_strategy(false);
            ret["evs"] = trainable->dump_evs();

            auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(trainable);
            if (preflop_trainable && preflop_trainable->isLocked()) {
                ret["is_locked"] = true;
                ret["locked_strategy"] = preflop_trainable->getLockedStrategy();
                ret["locked_mask"] = preflop_trainable->getLockedMask();
            }
        }
    } else if (node->getType() == PreflopGameTreeNode::NodeType::SHOWDOWN) {
        ret["node_type"] = "showdown_node";
    } else if (node->getType() == PreflopGameTreeNode::NodeType::TERMINAL) {
        auto terminal_node = dynamic_pointer_cast<PreflopTerminalNode>(node);
        ret["node_type"] = "terminal_node";
        ret["winner"] = terminal_node->getWinner();
    }
    return ret;
}

void PreflopCfrSolver::loads_strategy_and_evs(const shared_ptr<PreflopGameTreeNode>& node, const json& data) {
    if (!node || data.is_null()) return;

    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        shared_ptr<Trainable> trainable = action_node->getTrainable();
        if (!trainable) {
            auto actions = action_node->getActions();
            trainable = make_shared<PreflopTrainable>(&player_ranges[action_node->getPlayer()], actions.size());
            action_node->setTrainable(trainable);
        }

        auto preflop_trainable = dynamic_pointer_cast<PreflopTrainable>(trainable);
        if (preflop_trainable) {
            int action_num = action_node->getActions().size();
            int hand_num = player_ranges[action_node->getPlayer()].size();
            vector<float> loaded_cum_r(action_num * hand_num, 0.0f);
            vector<float> loaded_evs(action_num * hand_num, 0.0f);

            bool has_strat = false;
            if (data.contains("strategy") && data["strategy"].contains("strategy")) {
                has_strat = true;
                json strategy_json = data["strategy"]["strategy"];
                for (int i = 0; i < hand_num; ++i) {
                    string hand_str = player_ranges[action_node->getPlayer()][i].toString();
                    if (strategy_json.contains(hand_str)) {
                        vector<float> strat_vals = strategy_json[hand_str].get<vector<float>>();
                        for (int j = 0; j < action_num; ++j) {
                            if (j < (int)strat_vals.size()) {
                                loaded_cum_r[j * hand_num + i] = strat_vals[j];
                            }
                        }
                    }
                }
            }

            if (data.contains("evs") && data["evs"].contains("evs")) {
                json evs_json = data["evs"]["evs"];
                for (int i = 0; i < hand_num; ++i) {
                    string hand_str = player_ranges[action_node->getPlayer()][i].toString();
                    if (evs_json.contains(hand_str)) {
                        vector<float> ev_vals = evs_json[hand_str].get<vector<float>>();
                        for (int j = 0; j < action_num; ++j) {
                            if (j < (int)ev_vals.size()) {
                                loaded_evs[j * hand_num + i] = ev_vals[j];
                            }
                        }
                    }
                }
            }

            if (has_strat) {
                preflop_trainable->load_strategy_and_evs(loaded_cum_r, loaded_evs);
            }

            if (data.contains("is_locked") && data["is_locked"].get<bool>()) {
                vector<float> locked_strat = data["locked_strategy"].get<vector<float>>();
                vector<bool> locked_mask = data["locked_mask"].get<vector<bool>>();
                preflop_trainable->lockStrategy(locked_strat, locked_mask);
            }
        }

        const auto& actions = action_node->getActions();
        const auto& children = action_node->getChildren();
        if (data.contains("children")) {
            json children_json = data["children"];
            for (size_t i = 0; i < actions.size(); ++i) {
                if (children_json.contains(actions[i])) {
                    loads_strategy_and_evs(children[i], children_json[actions[i]]);
                }
            }
        }
    }
}

void PreflopCfrSolver::initialize_all_trainables(const shared_ptr<PreflopGameTreeNode>& node) {
    if (!node) return;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        int player = action_node->getPlayer();
        if (player >= 0 && player < num_players && !action_node->getTrainable()) {
            auto trainable = make_shared<PreflopTrainable>(&player_ranges[player], action_node->getActions().size());
            action_node->setTrainable(trainable);
        }
        for (const auto& child : action_node->getChildren()) {
            initialize_all_trainables(child);
        }
    }
}

void PreflopCfrSolver::update_current_strategies(const shared_ptr<PreflopGameTreeNode>& node, vector<vector<float>> reach_probs) {
    if (!node) return;
    if (node->getType() == PreflopGameTreeNode::NodeType::ACTION) {
        auto action_node = dynamic_pointer_cast<PreflopActionNode>(node);
        auto trainable = dynamic_pointer_cast<PreflopTrainable>(action_node->getTrainable());
        if (trainable) {
            trainable->computeCurrentStrategy();
        }
        
        int acting_player = action_node->getPlayer();
        const vector<float>& current_strategy = trainable->getcurrentStrategy();
        int acting_hand_num = player_ranges[acting_player].size();
        const auto& actions = action_node->getActions();
        const auto& children = action_node->getChildren();
        
        for (size_t a = 0; a < actions.size(); ++a) {
            vector<vector<float>> new_reach = reach_probs;
            for (int h = 0; h < acting_hand_num; ++h) {
                new_reach[acting_player][h] *= current_strategy[a * acting_hand_num + h];
            }
            update_current_strategies(children[a], new_reach);
        }
    } else if (node->getType() == PreflopGameTreeNode::NodeType::SHOWDOWN) {
#ifdef USE_LIBTORCH
        if (use_neural_net && nn_evaluator && nn_evaluator->isLoaded()) {
            auto showdown_node = dynamic_pointer_cast<PreflopShowdownNode>(node);
            const PreflopRule& rule = showdown_node->getRule();
            
            std::vector<int> active_players_vec;
            for (int p = 0; p < num_players; ++p) {
                if (rule.active_players[p]) active_players_vec.push_back(p);
            }
            
            if (active_players_vec.size() == 2) {
                int oop_player = active_players_vec[0];
                int ip_player = active_players_vec[1];

                // Compute exact canonical ranges
                std::vector<float> oop_range_vec(1326, 0.0f);
                std::vector<float> ip_range_vec(1326, 0.0f);
                
                auto combo_index = [](int c1, int c2) -> int {
                    int lo = std::min(c1, c2);
                    int hi = std::max(c1, c2);
                    return lo * 52 - lo * (lo + 1) / 2 + (hi - lo - 1);
                };

                for (size_t i = 0; i < player_ranges[oop_player].size(); ++i) {
                    const auto& pc = player_ranges[oop_player][i];
                    oop_range_vec[combo_index(pc.card1, pc.card2)] = reach_probs[oop_player][i];
                }
                for (size_t i = 0; i < player_ranges[ip_player].size(); ++i) {
                    const auto& pc = player_ranges[ip_player][i];
                    ip_range_vec[combo_index(pc.card1, pc.card2)] = reach_probs[ip_player][i];
                }

                float pot_val = rule.get_pot();
                float stack_val = rule.get_remaining_stack(oop_player);
                float spr_val = (pot_val > 0.0f) ? (stack_val / pot_val) : 0.0f;

                auto evs = nn_evaluator->evaluate(
                    pot_val, stack_val, spr_val,
                    oop_range_vec, ip_range_vec,
                    0, 0, 0, 0, 0, 0
                );
                
                showdown_node->nn_oop_evs = evs.first;
                showdown_node->nn_ip_evs = evs.second;
                showdown_node->has_nn_evs = true;
            } else {
                showdown_node->has_nn_evs = false;
            }
        }
#endif
    }
}
