#define __HIP_PLATFORM_AMD__ 
#include <hip/hip_runtime.h>
#include "include/solver/HipPCfrSolver.h"
#include <include/solver/BestResponse.h>
#include <include/nodes/ActionNode.h>
#include <include/nodes/ChanceNode.h>
#include <include/nodes/ShowdownNode.h>
#include <include/nodes/TerminalNode.h>
#include <include/tools/utils.h>
#include <include/tools/half-1-12-0.h>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <thread>
#include <unordered_map>
// Forward declarations of HIP helper wrappers
extern "C" {
    bool hip_initialize_device();
    void* hip_allocate_device(size_t size);
    void hip_free_device(void* ptr);
    void hip_copy_to_device(void* dst, const void* src, size_t size);
    void hip_copy_to_host(void* dst, const void* src, size_t size);
    void hip_memset_device(void* ptr, int value, size_t size);

    void hip_launch_forward_pass(
        const GpuState* states, int num_states, int level_start, int level_count, int next_level_start,
        void* regrets, const void* cum_strategies, const void* reach_probs_in, void* reach_probs_out,
        int range1_size, int range2_size, bool use_fp16,
        const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
        const uint8_t* locked, hipStream_t stream);

    void hip_launch_backward_pass(
        const GpuState* states, int num_states, int level_start, int level_count,
        void* regrets, void* cum_strategies,
        const void* reach_probs, const void* leaf_utilities, const void* utilities_in, void* utilities_out, int next_level_start,
        int range1_size, int range2_size,
        const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
        const float* dcfr_coeffs, bool use_fp16, const uint8_t* locked, hipStream_t stream);

    void hip_launch_showdown_pass(
        const GpuState* states, int n_sd,
        const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,
        const void* reach_probs, void* leaf_utilities,
        int range1_size, int range2_size,
        float win_payoff, float lose_payoff, int level_start, bool use_fp16, hipStream_t stream
    );

    void hip_launch_terminal_pass(
        const GpuState* states, int num_states, int level_count, int level_start,
        const void* reach_probs, void* leaf_utilities,
        int range1_size, int range2_size,
        const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
        const int* p0_to_p1_map, const int* p1_to_p0_map,
        bool use_fp16, hipStream_t stream
    );
}

HipPCfrSolver::HipPCfrSolver(
        std::shared_ptr<GameTree> tree,
        std::vector<PrivateCards> range1,
        std::vector<PrivateCards> range2,
        std::vector<int> initial_board,
        std::shared_ptr<Compairer> compairer,
        Deck deck,
        int iteration_number,
        int print_interval,
        std::string logfile,
        std::string trainer,
        float accuracy,
        bool use_fp16
        )
    : PCfrSolver(
                tree, range1, range2, initial_board, compairer, deck,
                iteration_number, false/*debug*/, print_interval, logfile, trainer,
                Solver::MonteCarolAlg::NONE, 0/*warmup*/, accuracy,
                true/*use_isomorphism*/, use_fp16/*use_halffloats*/, 12/*num_threads*/
                )
{

    this->use_fp16 = use_fp16;

    // Build flat GPU state representations
    buildGpuStates();

    // Prepare showdown indexes
    setupShowdowns();

    // Allocate VRAM
    allocateDeviceMemory();
}

HipPCfrSolver::~HipPCfrSolver() {
    freeDeviceMemory();
}

void reportGpuMemory(const std::string& label) {
    size_t free_bytes = 0;
    size_t total_bytes = 0;

    hipError_t err = hipMemGetInfo(&free_bytes, &total_bytes);
    if (err != hipSuccess) {
        std::cerr << "Failed to get memory info: " << hipGetErrorString(err) << "\n";
        return;
    }

    double free_gb = static_cast<double>(free_bytes) / (1024.0 * 1024.0 * 1024.0);
    double total_gb = static_cast<double>(total_bytes) / (1024.0 * 1024.0 * 1024.0);
    double used_gb = total_gb - free_gb;

    std::cout << "[" << label << "] VRAM Usage: " 
        << used_gb << " GB Used / " 
        << total_gb << " GB Total (" 
        << free_gb << " GB Free)\n";
}

struct BFSNode {
    std::shared_ptr<GameTreeNode> node;
    int deal_id;
    int parent_idx;
    int action_idx;
    int my_state_idx;
};

void HipPCfrSolver::buildGpuStates() {
    h_states.clear();
    action_state_map.clear();
    total_trainable_size = 0;
    total_leaf_nodes = 0;
    h_levels.clear();

    std::queue<BFSNode> q;

    // Create root state placeholder
    h_states.push_back(GpuState());
    q.push({tree->getRoot(), 0, -1, 0, 0});

    while (!q.empty()) {
        int level_size = q.size();
        int level_start = q.front().my_state_idx;
        h_levels.push_back({level_start, level_size});

        for (int q_idx = 0; q_idx < level_size; ++q_idx) {
            BFSNode current = q.front();
            q.pop();

        auto node = current.node;
        int my_idx = current.my_state_idx;

        GpuState state;
        int node_type = (int)node->getType();
        int player = -1;
        int action_count = 0;
        int turn_card = -1;
        int river_card = -1;
        state.deal_id = current.deal_id;
        state.parent_idx = current.parent_idx;
        state.action_idx_in_parent = current.action_idx;
        state.trainable_offset = -1;
        state.range_size = 0;
        state.pot = (float)node->getPot();
        state.child_start_idx = -1;

        int card_num = deck.getCards().size();
        if (current.deal_id > 0 && current.deal_id <= card_num) {
            int turn_card_idx = current.deal_id - 1;
            turn_card = deck.getCards()[turn_card_idx].getCardInt();
        } else if (current.deal_id > card_num) {
            int c_deal = current.deal_id - (1 + card_num);
            int turn_card_idx = c_deal / card_num;
            int river_card_idx = c_deal % card_num;
            turn_card = deck.getCards()[turn_card_idx].getCardInt();
            river_card = deck.getCards()[river_card_idx].getCardInt();
        }
        turn_card = turn_card;
        river_card = river_card;

        if (node->getType() == GameTreeNode::ACTION) {
            auto action_node = std::dynamic_pointer_cast<ActionNode>(node);
            player = action_node->getPlayer();
            action_count = action_node->getChildrens().size();
            state.range_size = ranges[player].size();

            state.trainable_offset = total_trainable_size;
            total_trainable_size += action_count * state.range_size;

            action_state_map[{action_node.get(), current.deal_id}] = my_idx;

            auto& children = action_node->getChildrens();
            if (!children.empty()) {
                state.child_start_idx = h_states.size(); // Next slots reserved for children
                for (size_t i = 0; i < children.size(); ++i) {
                    int child_idx = h_states.size();
                    h_states.push_back(GpuState()); // Reserve contiguous space
                    q.push({children[i], current.deal_id, my_idx, (int)i, child_idx});
                }
            }
        } else if (node->getType() == GameTreeNode::CHANCE) {
            auto chance_node = std::dynamic_pointer_cast<ChanceNode>(node);
            auto& chance_cards = chance_node->getCards();
            auto child = chance_node->getChildren();

            int card_num = deck.getCards().size();

            std::vector<std::pair<int, int>> valid_children; // <card_idx, new_deal_id>

            for (size_t c = 0; c < chance_cards.size(); ++c) {
                Card card = chance_cards[c];
                uint64_t card_long = Card::boardInt2long(card.getCardInt());

                uint64_t parent_board_long = initial_board_long;
                if (current.deal_id > 0 && current.deal_id <= card_num) {
                    // Turn to River: Walk up to find the parent ChanceNode that dealt the Turn card
                    std::shared_ptr<ChanceNode> parent_chance = nullptr;
                    auto p = current.node->getParent();
                    while (p) {
                        parent_chance = std::dynamic_pointer_cast<ChanceNode>(p);
                        if (parent_chance) break;
                        p = p->getParent();
                    }
                    if (!parent_chance) {
                        throw std::runtime_error("Could not find Turn ChanceNode parent!");
                    }
                    int turn_c = current.deal_id - 1;
                    Card turn_card = parent_chance->getCards()[turn_c];
                    parent_board_long |= Card::boardInt2long(turn_card.getCardInt());
                }

                if (Card::boardsHasIntercept(card_long, parent_board_long)) continue;

                int new_deal_id = 0;
                if (current.deal_id == 0) {
                    new_deal_id = c + 1;
                } else if (current.deal_id > 0 && current.deal_id <= card_num) {
                    int turn_card_idx = current.deal_id - 1;
                    new_deal_id = card_num * turn_card_idx + c + (1 + card_num);
                } else {
                    throw std::runtime_error("deal_id out of range in buildGpuStates");
                }
                valid_children.push_back({c, new_deal_id});
            }

            action_count = valid_children.size(); // Pass exact valid child count to GPU

            if (!valid_children.empty()) {
                state.child_start_idx = h_states.size();
                for (size_t i = 0; i < valid_children.size(); ++i) {
                    int child_idx = h_states.size();
                    h_states.push_back(GpuState());
                    q.push({child, valid_children[i].second, my_idx, (int)i, child_idx});
                }
            }
        } else if (node->getType() == GameTreeNode::TERMINAL) {
            auto terminal_node = std::dynamic_pointer_cast<TerminalNode>(node);
            auto payoffs = terminal_node->get_payoffs();
            if (payoffs[0] < 0) {
                player = 0; // Player 0 folded
                state.pot = (float)-payoffs[0];
            } else {
                player = 1; // Player 1 folded
                state.pot = (float)-payoffs[1];
            }
        }

        if (node_type == (int)GameTreeNode::TERMINAL || node_type == (int)GameTreeNode::SHOWDOWN) {
            state.child_start_idx = this->total_leaf_nodes++;
        }


        uint32_t packed = 0;
        packed |= (node_type & 0x3);
        packed |= ((player + 1) & 0x3) << 2;
        packed |= (action_count & 0xFF) << 4;
        packed |= ((turn_card == -1 ? 127 : turn_card) & 0x7F) << 12;
        packed |= ((river_card == -1 ? 127 : river_card) & 0x7F) << 19;
        state.packed = packed;
        
        // Save the fully configured state into its reserved slot
        h_states[my_idx] = state;
        } // end for loop over level elements
    }
    num_levels = h_levels.size();

    h_regrets.resize(total_trainable_size, 0.0f);
    h_cum_strategies.resize(total_trainable_size, 0.0f);
    h_locked.resize((total_trainable_size + 7) / 8, 0);
}

void HipPCfrSolver::setupShowdowns() {
    h_showdowns.clear();
    h_showdown_combs.clear();
    h_sd_levels.clear();

    struct CacheEntry { int p_start, o_start, p_size, o_size; };
    std::unordered_map<uint64_t, CacheEntry> board_to_comb_starts;

    for (size_t L = 0; L < h_levels.size(); ++L) {
        int level_start = h_levels[L].first;
        int level_size = h_levels[L].second;
        int sd_start = h_showdowns.size();
        int sd_count = 0;

        for (int i = level_start; i < level_start + level_size; ++i) {
            if (unpack_node_type(h_states[i].packed) == (int)GameTreeNode::SHOWDOWN) {
                int deal = h_states[i].deal_id;

                // Reconstruct board for this deal combination
                uint64_t board_long = initial_board_long;
                int card_num = deck.getCards().size();

                // Extract Turn & River card indexes from deal ID
                if (deal > 0 && deal <= card_num) {
                    int turn_card_idx = deal - 1;
                    board_long |= Card::boardInt2long(deck.getCards()[turn_card_idx].getCardInt());
                } else if (deal > card_num) {
                    int c_deal = deal - (1 + card_num);
                    int turn_card_idx = c_deal / card_num;
                    int river_card_idx = c_deal % card_num;
                    board_long |= Card::boardInt2long(deck.getCards()[turn_card_idx].getCardInt());
                    board_long |= Card::boardInt2long(deck.getCards()[river_card_idx].getCardInt());
                }

                ShowdownPrecalc calc;
                calc.state_idx = i;
                
                auto cache_it = board_to_comb_starts.find(board_long);
                if (cache_it != board_to_comb_starts.end()) {
                    calc.player_comb_start = cache_it->second.p_start;
                    calc.oppo_comb_start = cache_it->second.o_start;
                    calc.player_size = cache_it->second.p_size;
                    calc.oppo_size = cache_it->second.o_size;
                } else {
                    // Get river combos sorted by rank
                    auto& oop_private_cards = ranges[0];
                    auto& ip_private_cards = ranges[1];

                    auto oop_combs = rrm.getRiverCombos(0, oop_private_cards, board_long);
                    auto ip_combs = rrm.getRiverCombos(1, ip_private_cards, board_long);

                    calc.player_size = oop_combs.size();
                    calc.oppo_size = ip_combs.size();
                    calc.player_comb_start = h_showdown_combs.size();

                    for (const auto& comb : oop_combs) {
                        GpuShowdownComb gpu_comb;
                        gpu_comb.reach_prob_index = comb.reach_prob_index;
                        gpu_comb.rank = comb.rank;
                        gpu_comb.card1 = comb.private_cards.card1;
                        gpu_comb.card2 = comb.private_cards.card2;
                        h_showdown_combs.push_back(gpu_comb);
                    }

                    calc.oppo_comb_start = h_showdown_combs.size();
                    for (const auto& comb : ip_combs) {
                        GpuShowdownComb gpu_comb;
                        gpu_comb.reach_prob_index = comb.reach_prob_index;
                        gpu_comb.rank = comb.rank;
                        gpu_comb.card1 = comb.private_cards.card1;
                        gpu_comb.card2 = comb.private_cards.card2;
                        h_showdown_combs.push_back(gpu_comb);
                    }
                    
                    board_to_comb_starts[board_long] = {calc.player_comb_start, calc.oppo_comb_start, calc.player_size, calc.oppo_size};
                }

                h_showdowns.push_back(calc);
                sd_count++;
            }
        }
        h_sd_levels.push_back({sd_start, sd_count});
    }
}

void HipPCfrSolver::allocateDeviceMemory() {
    if (!hip_initialize_device()) {
        throw std::runtime_error("Failed to initialize AMD GPU device via HIP.");
    }

    int least_priority, greatest_priority;
    hipDeviceGetStreamPriorityRange(&least_priority, &greatest_priority);
    hipStreamCreateWithPriority(&compute_stream, hipStreamDefault, least_priority);

    // Run the detailed prediction dashboard before allocations hit the driver
    printExpectedVramUsage();
    reportGpuMemory("Pre-Allocation Baseline");

    size_t precision_size = use_fp16 ? sizeof(uint16_t) : sizeof(float);

    // Allocate GPU states
    d_states = (GpuState*)hip_allocate_device(h_states.size() * sizeof(GpuState));
    hip_copy_to_device(d_states, h_states.data(), h_states.size() * sizeof(GpuState));

    // Allocate regrets, strategies, and cumulative strategies buffers
    d_regrets = hip_allocate_device(total_trainable_size * precision_size);
    d_cum_strategies = hip_allocate_device(total_trainable_size * precision_size);
    size_t locked_bytes = (total_trainable_size + 7) / 8;
    d_locked = (uint8_t*)hip_allocate_device(locked_bytes);

    hip_memset_device(d_regrets, 0, total_trainable_size * precision_size);
    hip_memset_device(d_cum_strategies, 0, total_trainable_size * precision_size);
    hip_memset_device(d_locked, 0, (total_trainable_size + 7) / 8);


    // Allocate reach probabilities and utilities flat arrays
    // We MUST store both players' data independently at every state!
    int total_node_data_size = ranges[0].size() + ranges[1].size();

    int max_level_size = 0;
    for (const auto& pair : h_levels) {
        if (pair.second > max_level_size) max_level_size = pair.second;
    }

    size_t utilities_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float);
    size_t leaf_utilities_bytes = (size_t)total_leaf_nodes * total_node_data_size * sizeof(float);
    size_t reach_probs_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float);

    d_reach_probs_in = hip_allocate_device(reach_probs_bytes);
    d_reach_probs_out = hip_allocate_device(reach_probs_bytes);
    d_root_reach_probs = hip_allocate_device(total_node_data_size * sizeof(float));
    
    d_leaf_utilities = hip_allocate_device(leaf_utilities_bytes);
    d_utilities_in = hip_allocate_device(utilities_bytes);
    d_utilities_out = hip_allocate_device(utilities_bytes);

    hip_memset_device(d_reach_probs_in, 0, reach_probs_bytes);
    hip_memset_device(d_reach_probs_out, 0, reach_probs_bytes);
    
    hip_memset_device(d_leaf_utilities, 0, leaf_utilities_bytes);
    hip_memset_device(d_utilities_in, 0, utilities_bytes);
    hip_memset_device(d_utilities_out, 0, utilities_bytes);

    if (!h_showdowns.empty()) {
        d_showdowns = (ShowdownPrecalc*)hip_allocate_device(h_showdowns.size() * sizeof(ShowdownPrecalc));
        hip_copy_to_device(d_showdowns, h_showdowns.data(), h_showdowns.size() * sizeof(ShowdownPrecalc));

        d_showdown_combs = (GpuShowdownComb*)hip_allocate_device(h_showdown_combs.size() * sizeof(GpuShowdownComb));
        hip_copy_to_device(d_showdown_combs, h_showdown_combs.data(), h_showdown_combs.size() * sizeof(GpuShowdownComb));
    }

    // Upload range card data for blocker calculations
    {
        std::vector<GpuRangeCard> h_range_cards_0(ranges[0].size());
        for (size_t i = 0; i < ranges[0].size(); ++i) {
            h_range_cards_0[i].card1 = ranges[0][i].card1;
            h_range_cards_0[i].card2 = ranges[0][i].card2;
        }
        d_range_cards_0 = (GpuRangeCard*)hip_allocate_device(h_range_cards_0.size() * sizeof(GpuRangeCard));
        hip_copy_to_device(d_range_cards_0, h_range_cards_0.data(), h_range_cards_0.size() * sizeof(GpuRangeCard));

        std::vector<GpuRangeCard> h_range_cards_1(ranges[1].size());
        for (size_t i = 0; i < ranges[1].size(); ++i) {
            h_range_cards_1[i].card1 = ranges[1][i].card1;
            h_range_cards_1[i].card2 = ranges[1][i].card2;
        }
        d_range_cards_1 = (GpuRangeCard*)hip_allocate_device(h_range_cards_1.size() * sizeof(GpuRangeCard));
        hip_copy_to_device(d_range_cards_1, h_range_cards_1.data(), h_range_cards_1.size() * sizeof(GpuRangeCard));
    }

    // Upload player-to-player index mapping for same-hand correction
    {
        std::vector<int> h_p0_to_p1(ranges[0].size(), -1);
        std::vector<int> h_p1_to_p0(ranges[1].size(), -1);
        for (size_t i = 0; i < ranges[0].size(); ++i) {
            h_p0_to_p1[i] = pcm.indPlayer2Player(0, 1, i);
        }
        for (size_t i = 0; i < ranges[1].size(); ++i) {
            h_p1_to_p0[i] = pcm.indPlayer2Player(1, 0, i);
        }
        d_p0_to_p1_map = (int*)hip_allocate_device(h_p0_to_p1.size() * sizeof(int));
        hip_copy_to_device(d_p0_to_p1_map, h_p0_to_p1.data(), h_p0_to_p1.size() * sizeof(int));
        d_p1_to_p0_map = (int*)hip_allocate_device(h_p1_to_p0.size() * sizeof(int));
        hip_copy_to_device(d_p1_to_p0_map, h_p1_to_p0.data(), h_p1_to_p0.size() * sizeof(int));
    }

    // Allocate level boundaries and DCFR coefficients
    {
        std::vector<int> h_level_starts;
        std::vector<int> h_level_counts;
        for (const auto& pair : h_levels) {
            h_level_starts.push_back(pair.first);
            h_level_counts.push_back(pair.second);
        }
        d_level_starts = (int*)hip_allocate_device(h_level_starts.size() * sizeof(int));
        hip_copy_to_device(d_level_starts, h_level_starts.data(), h_level_starts.size() * sizeof(int));

        d_level_counts = (int*)hip_allocate_device(h_level_counts.size() * sizeof(int));
        hip_copy_to_device(d_level_counts, h_level_counts.data(), h_level_counts.size() * sizeof(int));

        d_dcfr_coeffs = (float*)hip_allocate_device(2 * sizeof(float));
    }

    reportGpuMemory("Post-Allocation Baseline");
}

void HipPCfrSolver::freeDeviceMemory() {
    if (compute_stream) { 
        hipStreamDestroy(compute_stream); 
        compute_stream = nullptr; 
    }
    if (d_states) { hip_free_device(d_states); d_states = nullptr; }
    if (d_regrets) { hip_free_device(d_regrets); d_regrets = nullptr; }
    if (d_cum_strategies) { hip_free_device(d_cum_strategies); d_cum_strategies = nullptr; }
    if (d_reach_probs_in) { hip_free_device(d_reach_probs_in); d_reach_probs_in = nullptr; }
    if (d_reach_probs_out) { hip_free_device(d_reach_probs_out); d_reach_probs_out = nullptr; }
    if (d_root_reach_probs) { hip_free_device(d_root_reach_probs); d_root_reach_probs = nullptr; }
    if (d_leaf_utilities) { hip_free_device(d_leaf_utilities); d_leaf_utilities = nullptr; }
    if (d_utilities_in) { hip_free_device(d_utilities_in); d_utilities_in = nullptr; }
    if (d_utilities_out) { hip_free_device(d_utilities_out); d_utilities_out = nullptr; }
    if (d_locked) { hip_free_device(d_locked); d_locked = nullptr; }

    if (d_showdowns) { hip_free_device(d_showdowns); d_showdowns = nullptr; }
    if (d_showdown_combs) { hip_free_device(d_showdown_combs); d_showdown_combs = nullptr; }
    if (d_range_cards_0) { hip_free_device(d_range_cards_0); d_range_cards_0 = nullptr; }
    if (d_range_cards_1) { hip_free_device(d_range_cards_1); d_range_cards_1 = nullptr; }
    if (d_p0_to_p1_map) { hip_free_device(d_p0_to_p1_map); d_p0_to_p1_map = nullptr; }
    if (d_p1_to_p0_map) { hip_free_device(d_p1_to_p0_map); d_p1_to_p0_map = nullptr; }
    if (d_level_starts) { hip_free_device(d_level_starts); d_level_starts = nullptr; }
    if (d_level_counts) { hip_free_device(d_level_counts); d_level_counts = nullptr; }
    if (d_dcfr_coeffs) { hip_free_device(d_dcfr_coeffs); d_dcfr_coeffs = nullptr; }

    if (hip_graph_exec) { hipGraphExecDestroy(hip_graph_exec); hip_graph_exec = nullptr; }
    if (hip_graph) { hipGraphDestroy(hip_graph); hip_graph = nullptr; }
}

void HipPCfrSolver::copyToDevice() {
    size_t precision_size = use_fp16 ? sizeof(uint16_t) : sizeof(float);

    if (use_fp16) {
        std::vector<half_float::half> temp_fp16_regrets(total_trainable_size);
        std::vector<half_float::half> temp_fp16_cum(total_trainable_size);

#pragma omp parallel for schedule(static)
        for (int i = 0; i < total_trainable_size; ++i) {
            temp_fp16_regrets[i] = half_float::half(h_regrets[i]);
            temp_fp16_cum[i] = half_float::half(h_cum_strategies[i]);
        }

        hip_copy_to_device(d_regrets, temp_fp16_regrets.data(), total_trainable_size * precision_size);
        hip_copy_to_device(d_cum_strategies, temp_fp16_cum.data(), total_trainable_size * precision_size);
    } else {
        // Initial regrets are 0, but if we resume or set custom values:
        hip_copy_to_device(d_regrets, h_regrets.data(), total_trainable_size * precision_size);
        hip_copy_to_device(d_cum_strategies, h_cum_strategies.data(), total_trainable_size * precision_size);
    }
}

void HipPCfrSolver::copyFromDevice() {
    // Copy the final cumulative strategy weights back to the host
    if (use_fp16) {
        std::vector<half_float::half> temp_fp16(total_trainable_size);
        hip_copy_to_host(temp_fp16.data(), d_cum_strategies, total_trainable_size * sizeof(uint16_t));

        // Convert FP16 back to FP32 for CPU storage
#pragma omp parallel for schedule(static)
        for (int i = 0; i < total_trainable_size; ++i) {
            h_cum_strategies[i] = static_cast<float>(temp_fp16[i]);
        }
    } else {
        hip_copy_to_host(h_cum_strategies.data(), d_cum_strategies, total_trainable_size * sizeof(float));
    }
}

void HipPCfrSolver::train() {
    std::vector<std::vector<PrivateCards>> player_privates(this->player_number);
    player_privates[0] = pcm.getPreflopCards(0);
    player_privates[1] = pcm.getPreflopCards(1);
    if(this->use_isomorphism){
        this->findGameSpecificIsomorphisms();
    }

    BestResponse br = BestResponse(player_privates,this->player_number,this->pcm,this->rrm,this->deck,this->debug,this->color_iso_offset,this->split_round,this->num_threads,this->use_halffloats);

    // Initial exploitability check
    syncStrategiesToCpuTree();
    this->last_exploitability = br.printExploitability(tree->getRoot(), 0, tree->getRoot()->getPot(), initial_board_long);
    this->last_iteration = 0;


    // Showdown payoffs
    float win_payoff = 1.0f;  // Standard pot win utility scaling multiplier
    float lose_payoff = -1.0f;

    std::cout << "Starting GPU Solver in " << (use_fp16 ? "FP16 half-precision" : "FP32 single-precision") << " mode." << std::endl;


    // Set initial reach probabilities at root for BOTH players
    int total_node_data_size = ranges[0].size() + ranges[1].size();
    std::vector<float> root_reach_probs(total_node_data_size);
    for (size_t i = 0; i < ranges[0].size(); ++i) {
        root_reach_probs[i] = ranges[0][i].weight;
    }
    for (size_t i = 0; i < ranges[1].size(); ++i) {
        root_reach_probs[ranges[0].size() + i] = ranges[1][i].weight;
    }
    hip_copy_to_device(d_reach_probs_in, root_reach_probs.data(), root_reach_probs.size() * sizeof(float));
    hip_copy_to_device(d_root_reach_probs, root_reach_probs.data(), root_reach_probs.size() * sizeof(float));
    std::vector<float> check_reach(1);
    hip_copy_to_host(check_reach.data(), d_reach_probs_in, sizeof(float));

    int max_level_size = 0;
    for (const auto& pair : h_levels) {
        if (pair.second > max_level_size) max_level_size = pair.second;
    }
    size_t utilities_bytes_size = max_level_size * total_node_data_size * sizeof(float);

    float* h_async_buffer = nullptr;
    hipHostMalloc(&h_async_buffer, total_trainable_size * sizeof(float));

    // BUILD HIP GRAPH via stream capture
    if (!graph_created) {
        // Initial locks sync
        std::fill(h_regrets.begin(), h_regrets.end(), 0.0f);
        std::fill(h_cum_strategies.begin(), h_cum_strategies.end(), 0.0f);
        std::fill(h_locked.begin(), h_locked.end(), 0);

        bool has_locks = false;
        for (auto const& [key, my_idx] : action_state_map) {
            ActionNode* action_node = key.first;
            int deal = key.second;
            auto trainable = action_node->getTrainable(deal, true, this->use_halffloats);
            if (trainable && trainable->isLocked()) {
                has_locks = true;
                int offset = h_states[my_idx].trainable_offset;
                int action_count = unpack_action_count(h_states[my_idx].packed);
                int range_size = h_states[my_idx].range_size;

                const auto& locked_mask = trainable->getLockedMask();
                const auto& locked_strat = trainable->getAverageStrategy();

                for (int p = 0; p < range_size; ++p) {
                    if (locked_mask.empty() || locked_mask[p]) {
                        for (int a = 0; a < action_count; ++a) {
                            int idx = offset + a * range_size + p;
                            h_locked[idx >> 3] |= (1 << (idx & 7));
                            h_cum_strategies[idx] = locked_strat[a * range_size + p];
                        }
                    }
                }
            }
        }

        if (has_locks) {
            copyToDevice();
        }
        hip_copy_to_device(d_locked, h_locked.data(), (total_trainable_size + 7) / 8);

        hipStreamBeginCapture(compute_stream, hipStreamCaptureModeGlobal);

        // 0. memset utilities
        hipMemsetAsync(d_utilities_in, 0, utilities_bytes_size, compute_stream);
        hipMemsetAsync(d_utilities_out, 0, utilities_bytes_size, compute_stream);
        hipMemsetAsync(d_leaf_utilities, 0, total_leaf_nodes * (ranges[0].size() + ranges[1].size()) * sizeof(float), compute_stream);

        // Restore root reach probabilities for Level 0
        hipMemcpyAsync(d_reach_probs_in, d_root_reach_probs, root_reach_probs.size() * sizeof(float), hipMemcpyDeviceToDevice, compute_stream);

        void* current_reach = d_reach_probs_in;
        void* next_reach = d_reach_probs_out;

        // 1. Forward Pass & Leaves (top-down)
        for (int L = 0; L < num_levels; ++L) {
            int next_level_start = (L + 1 < num_levels) ? h_levels[L+1].first : h_states.size();
            
            hip_launch_forward_pass(
                d_states, h_states.size(), h_levels[L].first, h_levels[L].second, next_level_start,
                d_regrets, d_cum_strategies, current_reach, next_reach,
                ranges[0].size(), ranges[1].size(), use_fp16,
                d_range_cards_0, d_range_cards_1, d_locked,
                compute_stream);

            int sd_start = h_sd_levels[L].first;
            int sd_count = h_sd_levels[L].second;
            if (sd_count > 0) {
                hip_launch_showdown_pass(
                    d_states, sd_count, d_showdowns + sd_start, d_showdown_combs,
                    (const float*)current_reach, (float*)d_leaf_utilities,
                    ranges[0].size(), ranges[1].size(),
                    win_payoff, lose_payoff, h_levels[L].first, use_fp16, compute_stream);
            }

            hip_launch_terminal_pass(
                d_states, h_states.size(), h_levels[L].second, h_levels[L].first,
                (const float*)current_reach, (float*)d_leaf_utilities,
                ranges[0].size(), ranges[1].size(),
                d_range_cards_0, d_range_cards_1,
                d_p0_to_p1_map, d_p1_to_p0_map, use_fp16, compute_stream);

            std::swap(current_reach, next_reach);
        }

        // 3. Backward Pass per level (bottom-up)
        void* current_util = d_utilities_in;
        void* next_util = d_utilities_out;

        for (int L = num_levels - 1; L >= 0; --L) {
            int next_level_start = (L + 1 < num_levels) ? h_levels[L+1].first : h_states.size();
            hip_launch_backward_pass(
                d_states, h_states.size(), h_levels[L].first, h_levels[L].second,
                d_regrets, d_cum_strategies,
                nullptr, d_leaf_utilities, current_util, next_util, next_level_start,
                ranges[0].size(), ranges[1].size(),
                d_range_cards_0, d_range_cards_1, d_dcfr_coeffs, use_fp16, d_locked,
                compute_stream);
            
            std::swap(current_util, next_util);
        }

        hipStreamEndCapture(compute_stream, &hip_graph);
        hipGraphInstantiate(&hip_graph_exec, hip_graph, nullptr, nullptr, 0);
        graph_created = true;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    auto last_checkpoint_time = start_time;

    // Run CFR training loops
    for (int iter = 0; iter < iteration_number; ++iter) {
        if (nowstop) break;

        // 0. Update DCFR Coefficients
        float f_iter = (float)(iter + 1);
        float alpha_coef = powf(f_iter, 1.5f);
        alpha_coef = alpha_coef / (1.0f + alpha_coef);
        float strategy_coef = powf(f_iter / (f_iter + 1.0f), 2.0f);
        
        float h_dcfr[2] = {alpha_coef, strategy_coef};
        hipMemcpyAsync(d_dcfr_coeffs, h_dcfr, 2 * sizeof(float), hipMemcpyHostToDevice, compute_stream);

        // 1. Launch the entire iteration via Graph
        hipGraphLaunch(hip_graph_exec, compute_stream);

        // 4. The Heartbeat (Keep the OS alive and ensure accurate timing)
        if (iter % print_interval == 0 || iter % 5 == 0) {
            hipDeviceSynchronize(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        if (iter > 0 && iter % print_interval == 0) {
            auto current_time = std::chrono::high_resolution_clock::now();

            std::chrono::duration<double> block_duration = current_time - last_checkpoint_time;
            std::chrono::duration<double> total_duration = current_time - start_time;
            double block_avg = block_duration.count() / print_interval;
            double total_avg = total_duration.count() / iter;
            std::cout << "Iteration " << iter << " GTO solve in progress...\n"
                << "  -> Last " << print_interval << " iters: " 
                << std::fixed << std::setprecision(3) << block_duration.count() << "s "
                << "(" << block_avg << " s/iter)\n"
                << "  -> Total avg so far: " << total_avg << " s/iter\n" 
                << "  -> Total duration so far: " << std::setprecision(3) << total_duration.count() << "s" << "\n" 
                << std::endl;

            this->last_iteration = iter;
            last_checkpoint_time = current_time;
        }

        if (iter > 0 && iter % 160 == 0) {
            syncStrategiesToCpuTree();
            calculateEvs();
            float exploitability = br.printExploitability(tree->getRoot(), iter, tree->getRoot()->getPot(), initial_board_long);
            this->last_exploitability = exploitability;
            this->last_iteration = iter;

            if (this->accuracy > 0.0f && exploitability <= this->accuracy) {
                std::cout << "Target accuracy of " << this->accuracy << "% reached (current exploitability: " << exploitability << "%). Stopping solver early." << std::endl;
                break;
            }
        }

    }

    std::cout << "Solve finished" << std::endl;

    syncStrategiesToCpuTree();

    calculateEvs();

    // Calculate final exploitability
    float final_exploitability = br.printExploitability(tree->getRoot(), iteration_number, tree->getRoot()->getPot(), initial_board_long);
    this->last_exploitability = final_exploitability;
    this->last_iteration = iteration_number;

    freeDeviceMemory();
}

void HipPCfrSolver::stop() {
    nowstop = true;
}

void HipPCfrSolver::printExpectedVramUsage() {
    size_t precision_size = use_fp16 ? sizeof(uint16_t) : sizeof(float);
    int total_node_data_size = ranges[0].size() + ranges[1].size();

    // 1. Calculate bytes for each discrete allocation
    size_t states_bytes = h_states.size() * sizeof(GpuState);
    size_t regrets_bytes = total_trainable_size * precision_size;
    size_t cum_strategies_bytes = total_trainable_size * precision_size;
    size_t locked_bytes = total_trainable_size * sizeof(uint8_t);

    int max_level_size = 0;
    for (const auto& pair : h_levels) {
        if (pair.second > max_level_size) max_level_size = pair.second;
    }
    
    // reach_probs_in and reach_probs_out
    size_t root_reach_probs_bytes = (size_t)total_node_data_size * sizeof(float);
    size_t reach_probs_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float) * 2 + root_reach_probs_bytes; // in, out, root
    
    // utilities_in and utilities_out
    size_t utilities_bytes = (size_t)max_level_size * total_node_data_size * sizeof(float) * 2;
    
    // leaf utilities
    size_t leaf_utilities_bytes = (size_t)total_leaf_nodes * total_node_data_size * sizeof(float);
    utilities_bytes += leaf_utilities_bytes;

    size_t showdowns_bytes = h_showdowns.size() * sizeof(ShowdownPrecalc);
    size_t showdown_combs_bytes = h_showdown_combs.size() * sizeof(GpuShowdownComb);

    size_t map_bytes = (ranges[0].size() + ranges[1].size()) * (sizeof(GpuRangeCard) + sizeof(int));

    size_t total_bytes = states_bytes + regrets_bytes + 
        cum_strategies_bytes + locked_bytes + reach_probs_bytes + utilities_bytes + 
        showdowns_bytes + showdown_combs_bytes + map_bytes;

    double total_mb = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
    double total_gb = total_mb / 1024.0;

    std::cout << "\n==================================================\n";
    std::cout << "          VRAM OCCUPATION PREDICTION             \n";
    std::cout << "==================================================\n";
    std::cout << "Precision Mode:          " << (use_fp16 ? "FP16 (2 Bytes)" : "FP32 (4 Bytes)") << "\n";
    std::cout << "Total Game States:       " << h_states.size() << "\n";
    std::cout << "Total Trainable Slots:   " << total_trainable_size << std::endl;
    std::cout << "Private Hand Combos:     " << total_node_data_size << " (Ranges OOP + IP)" << std::endl;
    std::cout << "--------------------------------------------------" << std::endl; std::cout << std::fixed << std::setprecision(2);
    std::cout << "d_states Buffer:         " << static_cast<double>(states_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "d_regrets Buffer:        " << static_cast<double>(regrets_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "d_cum_strategies Buffer: " << static_cast<double>(cum_strategies_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "d_locked Buffer:         " << static_cast<double>(locked_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "d_reach_probs Buffer:    " << static_cast<double>(reach_probs_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "d_utilities Buffer:      " << static_cast<double>(utilities_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "Showdown Precalc Buffers:" << static_cast<double>(showdowns_bytes + showdown_combs_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "Misc (Maps) Buffers:       " << static_cast<double>(map_bytes) / (1024.0 * 1024.0) << " MB\n";
    std::cout << "--------------------------------------------------\n";
    std::cout << "ESTIMATED TOTAL DEMAND:  " << total_mb << " MB (" << total_gb << " GB)\n";
    std::cout << "==================================================\n\n";

    size_t free_byte;
    size_t total_byte;
    hipError_t hip_status = hipMemGetInfo(&free_byte, &total_byte);

    if (hipSuccess != hip_status) {
        std::cerr << "Error: hipMemGetInfo fails, " << hipGetErrorString(hip_status) << std::endl;
        exit(1);
    }
    
    double free_gb = static_cast<double>(free_byte) / (1024.0 * 1024.0 * 1024.0);
    double threshold_gb = free_gb * 0.95;

    if (total_gb > threshold_gb) {
        std::cerr << "CRITICAL WARNING: Expected VRAM allocation exceeds a safe operating threshold (95% of free VRAM)!\n";
        std::cerr << "Required: " << total_gb << " GB, Available Cap: " << threshold_gb << " GB\n\n";
        exit(-1);
    }
}

// In src/solver/HipPCfrSolver.cpp

void HipPCfrSolver::syncStrategiesToCpuTree() {
    // 1. Copy the final cumulative strategy weights back to the host
    if (use_fp16) {
        std::vector<half_float::half> temp_fp16(total_trainable_size);
        hip_copy_to_host(temp_fp16.data(), d_cum_strategies, total_trainable_size * sizeof(uint16_t));

        // Convert FP16 back to FP32 for CPU storage in parallel
#pragma omp parallel for schedule(static)
        for (int i = 0; i < total_trainable_size; ++i) {
            h_cum_strategies[i] = static_cast<float>(temp_fp16[i]);
        }
    } else {
        hip_copy_to_host(h_cum_strategies.data(), d_cum_strategies, total_trainable_size * sizeof(float));
    }

    // DEBUG PRINT (Only if debug is enabled, parallelized)
    if (this->debug) {
        float max_cs = -999.0f, min_cs = 999.0f;
        int non_zero_count = 0;
#pragma omp parallel for reduction(max:max_cs) reduction(min:min_cs) reduction(+:non_zero_count) schedule(static)
        for (int i = 0; i < (int)h_cum_strategies.size(); ++i) {
            float val = h_cum_strategies[i];
            if (val > max_cs) max_cs = val;
            if (val < min_cs) min_cs = val;
            if (val != 0.0f) non_zero_count++;
        }
        std::cout << "DEBUG SYNC: h_cum_strategies stats: max=" << max_cs << ", min=" << min_cs << ", non-zero count=" << non_zero_count << "/" << total_trainable_size << std::endl;
    }

    // 2. Write flat cumulative strategies back to their corresponding ActionNode trainables in parallel
    std::vector<std::pair<std::pair<ActionNode*, int>, int>> entries(action_state_map.begin(), action_state_map.end());
#pragma omp parallel for schedule(dynamic)
    for (size_t idx = 0; idx < entries.size(); ++idx) {
        const auto& entry = entries[idx];
        ActionNode* action_node = entry.first.first;
        int deal = entry.first.second;
        int state_idx = entry.second;
        int trainable_offset = h_states[state_idx].trainable_offset;
        int action_count = unpack_action_count(h_states[state_idx].packed);
        int range_size = h_states[state_idx].range_size;

        auto trainable = action_node->getTrainable(deal, true, this->use_halffloats); // Match solver's precision format on CPU
        if (trainable) {
            std::vector<float> cum_strat(action_count * range_size);
            std::memcpy(cum_strat.data(), &h_cum_strategies[trainable_offset], action_count * range_size * sizeof(float));
            trainable->setCumRegrets(cum_strat);
        }
    }
}


// Inherited methods dumps, get_strategy, get_evs, get_trainable from PCfrSolver are used directly.

void HipPCfrSolver::calculateEvs() {
    std::cout << "Collecting final tree statistics (calculating average strategy EVs on CPU)..." << std::endl;
    this->collecting_statics = true;
    this->use_average_strategy_for_cfr = true;
    
    std::vector<std::vector<float>> reach_probs = this->getReachProbs();
    for (int player_id = 0; player_id < this->player_number; player_id++) {
        this->round_deal = std::vector<int>{-1, -1, -1, -1};
        cfr(player_id, this->tree->getRoot(), reach_probs[1 - player_id], this->iteration_number, this->initial_board_long, 0);
    }
    
    this->collecting_statics = false;
    this->use_average_strategy_for_cfr = false;
    this->statics_collected = true;
    std::cout << "Statistics collection complete. EVs calculated successfully." << std::endl;
}
