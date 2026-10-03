// Inherited methods dumps, get_strategy, get_evs, get_trainable from PCfrSolver are used directly.

#include <hip/hip_runtime.h>

#include <hip/hip_fp16.h>

#include <include/solver/GpuTypes.h>

__device__ inline bool is_locked(const uint8_t* locked_bits, int idx) {
    return (locked_bits[idx >> 3] >> (idx & 7)) & 1;
}


#include <iostream>

#include <cmath>



#define BLOCK_SIZE 128



template<typename T>

__device__ inline float read_val(const T* arr, int idx) {

    return (sizeof(T) == 2) ? __half2float(((const __half*)arr)[idx]) : ((const float*)arr)[idx];

}



template<typename T>

__device__ inline void write_val(T* arr, int idx, float val) {

    if (sizeof(T) == 2) {

        ((__half*)arr)[idx] = __float2half(val);

    } else {

        ((float*)arr)[idx] = val;

    }

}



extern "C" {

bool hip_initialize_device() { hipError_t err = hipSetDevice(0); return err == hipSuccess; }

void* hip_allocate_device(size_t size) { 
    void* ptr = nullptr; 
    hipError_t err = hipMalloc(&ptr, size); 
    if (err != hipSuccess) {
        printf("GPU Memory Allocation Failed for %zu bytes! Error %d\n", size, (int)err);
    }
    return ptr; 
}

void hip_free_device(void* ptr) { if (ptr) hipFree(ptr); }

void hip_copy_to_device(void* dst, const void* src, size_t size) { hipMemcpy(dst, src, size, hipMemcpyHostToDevice); }

void hip_copy_to_host(void* dst, const void* src, size_t size) { hipMemcpy(dst, src, size, hipMemcpyDeviceToHost); }

void hip_memset_device(void* ptr, int value, size_t size) { hipMemset(ptr, value, size); }

}



// ----------------------------------------------------------------------------

// STRATEGY COMPUTATION HELPER

// ----------------------------------------------------------------------------

template<typename T>
__device__ inline float compute_strategy_prob(
    const T* regrets, int offset, int active_range, int t, int action_count, int a
) {
    float r_sum = 0.0f;
    for (int i = 0; i < action_count; ++i) {
        float r = read_val(regrets, offset + i * active_range + t);
        if (r > 0.0f) r_sum += r;
    }
    if (r_sum > 0.0f) {
        float r = read_val(regrets, offset + a * active_range + t);
        return (r > 0.0f) ? (r / r_sum) : 0.0f;
    }
    return 1.0f / (float)action_count;
}

// ----------------------------------------------------------------------------

// FORWARD PASS

// ----------------------------------------------------------------------------

template<typename T>
__global__ void forward_pass_kernel(
    const GpuState* states, int num_states, int level_start, int level_count, int next_level_start,
    T* regrets, const T* cum_strategies, const T* reach_probs_in, T* reach_probs_out,
    int range1_size, int range2_size,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    const uint8_t* locked
) {
    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;
    int node_in_level = blockIdx.x;
    if (node_in_level >= level_count) return;

    int t = blockIdx.y * blockDim.x + threadIdx.x;
    if (t >= max_range_size) return;

    int node_data_size = range1_size + range2_size;
    int state_idx = level_start + node_in_level;

    const GpuState& state = states[state_idx];



        if (unpack_node_type(state.packed) == 0) { // ACTION node

            int action_count = unpack_action_count(state.packed);

            int offset = state.trainable_offset;

            int active_range = (unpack_player(state.packed) == 0) ? range1_size : range2_size;



// Strategy calculation is now done inline below

            // 2. Propagate reach for BOTH players
            for (int a = 0; a < action_count; ++a) {
                int child_idx = state.child_start_idx + a;
                if (child_idx < num_states) {
                    const GpuState& child_state = states[child_idx];
                    int p0_parent = node_in_level * node_data_size + t;
                    int p0_child  = (child_idx - next_level_start) * node_data_size + t;
                    int p1_parent = node_in_level * node_data_size + range1_size + t;
                    int p1_child  = (child_idx - next_level_start) * node_data_size + range1_size + t;

                    if (t < range1_size) {
                        int card1 = range_cards_0[t].card1;
                        int card2 = range_cards_0[t].card2;
                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||
                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {
                            write_val(reach_probs_out, p0_child, 0.0f);
                        } else {
                            if (unpack_player(state.packed) == 0) {
                                float strat;
                                if (is_locked(locked, offset + t)) {
                                    strat = read_val(cum_strategies, offset + a * range1_size + t);
                                } else {
                                    strat = compute_strategy_prob(regrets, offset, range1_size, t, action_count, a);
                                }
                                write_val(reach_probs_out, p0_child, read_val(reach_probs_in, p0_parent) * strat);
                            } else {
                                write_val(reach_probs_out, p0_child, read_val(reach_probs_in, p0_parent)); // Passive copy
                            }
                        }
                    }

                    if (t < range2_size) {
                        int card1 = range_cards_1[t].card1;
                        int card2 = range_cards_1[t].card2;
                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||
                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {
                            write_val(reach_probs_out, p1_child, 0.0f);
                        } else {
                            if (unpack_player(state.packed) == 1) {
                                float strat;
                                if (is_locked(locked, offset + t)) {
                                    strat = read_val(cum_strategies, offset + a * range2_size + t);
                                } else {
                                    strat = compute_strategy_prob(regrets, offset, range2_size, t, action_count, a);
                                }
                                write_val(reach_probs_out, p1_child, read_val(reach_probs_in, p1_parent) * strat);
                            } else {
                                write_val(reach_probs_out, p1_child, read_val(reach_probs_in, p1_parent)); // Passive copy
                            }
                        }
                    }
                }
            }
        } else if (unpack_node_type(state.packed) == 3) { // CHANCE node
            float scale = 1.0f / (float)(unpack_action_count(state.packed) - 2);
            for (int c = 0; c < unpack_action_count(state.packed); ++c) {
                int child_idx = state.child_start_idx + c;
                if (child_idx < num_states) {
                    const GpuState& child_state = states[child_idx];
                    int p0_parent = node_in_level * node_data_size + t;
                    int p0_child  = (child_idx - next_level_start) * node_data_size + t;
                    int p1_parent = node_in_level * node_data_size + range1_size + t;
                    int p1_child  = (child_idx - next_level_start) * node_data_size + range1_size + t;

                    if (t < range1_size) {
                        int card1 = range_cards_0[t].card1;
                        int card2 = range_cards_0[t].card2;
                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||
                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {
                            write_val(reach_probs_out, p0_child, 0.0f);
                        } else {
                            write_val(reach_probs_out, p0_child, read_val(reach_probs_in, p0_parent) * scale);
                        }
                    }

                    if (t < range2_size) {
                        int card1 = range_cards_1[t].card1;
                        int card2 = range_cards_1[t].card2;
                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||
                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {
                            write_val(reach_probs_out, p1_child, 0.0f);
                        } else {
                            write_val(reach_probs_out, p1_child, read_val(reach_probs_in, p1_parent) * scale);
                        }
                    }
                }
            }
        }
}



// ----------------------------------------------------------------------------

// SHOWDOWN PASS (Both players calculated simultaneously)

// ----------------------------------------------------------------------------

template<typename T>
__global__ void showdown_pass_kernel(
    const GpuState* states, int n_sd,
    const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,
    const T* reach_probs, T* leaf_utilities,
    int range1_size, int range2_size, float win_payoff, float lose_payoff,
    int level_start
) {
    int sd_idx = blockIdx.x;
    if (sd_idx >= n_sd) return;

    const ShowdownPrecalc& sd = showdowns[sd_idx];
    int state_idx = sd.state_idx;

    int node_data_size = range1_size + range2_size;
    int reach_p0_offset = (state_idx - level_start) * node_data_size;
    int reach_p1_offset = (state_idx - level_start) * node_data_size + range1_size;
    
    int util_p0_offset = states[state_idx].child_start_idx * node_data_size;
    int util_p1_offset = states[state_idx].child_start_idx * node_data_size + range1_size;

    // OOP (Player 0) Evaluation
    for (int t = threadIdx.x; t < sd.player_size; t += blockDim.x) {
        const GpuShowdownComb& p_comb = combs[sd.player_comb_start + t];
        float win_sum = 0.0f, lose_sum = 0.0f;
        for (int j = 0; j < sd.oppo_size; ++j) {
            const GpuShowdownComb& o_comb = combs[sd.oppo_comb_start + j];
            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||
                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;
            float oppo_reach = read_val(reach_probs, reach_p1_offset + o_comb.reach_prob_index);

            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;
            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;
            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }
        }
        write_val(leaf_utilities, util_p0_offset + p_comb.reach_prob_index, ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f));
    }

    // IP (Player 1) Evaluation (Mirrored)
    for (int t = threadIdx.x; t < sd.oppo_size; t += blockDim.x) {
        const GpuShowdownComb& p_comb = combs[sd.oppo_comb_start + t];
        float win_sum = 0.0f, lose_sum = 0.0f;
        for (int j = 0; j < sd.player_size; ++j) {
            const GpuShowdownComb& o_comb = combs[sd.player_comb_start + j];
            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||
                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;
            float oppo_reach = read_val(reach_probs, reach_p0_offset + o_comb.reach_prob_index);

            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;
            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;
            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }
        }
        write_val(leaf_utilities, util_p1_offset + p_comb.reach_prob_index, ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f));
    }


}



// ----------------------------------------------------------------------------

// TERMINAL PASS (Both players calculated simultaneously)

// ----------------------------------------------------------------------------

template<typename T>
__global__ void eval_terminal_nodes_kernel(
    const GpuState* states, int num_states, int level_start,
    const T* reach_probs, T* leaf_utilities,
    int range1_size, int range2_size,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    const int* p0_to_p1_map, const int* p1_to_p0_map
) {
    int node_in_level = blockIdx.x;
    int state_idx = level_start + node_in_level;

    if (state_idx >= num_states) return;

    const GpuState& state = states[state_idx];

    if (unpack_node_type(state.packed) != 2) return;

    int node_data_size = range1_size + range2_size;
    int reach_p0_offset = node_in_level * node_data_size;
    int reach_p1_offset = node_in_level * node_data_size + range1_size;
    
    int util_p0_offset = states[state_idx].child_start_idx * node_data_size;
    int util_p1_offset = states[state_idx].child_start_idx * node_data_size + range1_size;

    float fold_payoff = state.pot;

    // --- OOP (Player 0) Evaluation ---
    {
        float oppo_sum = 0.0f;
        float card_sum[52];
        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;

        for (int i = 0; i < range2_size; ++i) {
            float r = read_val(reach_probs, reach_p1_offset + i);
            oppo_sum += r;
            card_sum[range_cards_1[i].card1] += r;
            card_sum[range_cards_1[i].card2] += r;
        }

        for (int t = threadIdx.x; t < range1_size; t += blockDim.x) {
            int my_card1 = range_cards_0[t].card1;
            int my_card2 = range_cards_0[t].card2;
            float effective_oppo = oppo_sum - card_sum[my_card1] - card_sum[my_card2];

            int same_hand_idx = p0_to_p1_map[t];
            if (same_hand_idx >= 0) {
                effective_oppo += read_val(reach_probs, reach_p1_offset + same_hand_idx);
            }

            write_val(leaf_utilities, util_p0_offset + t, (unpack_player(state.packed) == 0) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo));
        }
    }

    // --- IP (Player 1) Evaluation ---
    {
        float oppo_sum = 0.0f;
        float card_sum[52];
        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;

        for (int i = 0; i < range1_size; ++i) {
            float r = read_val(reach_probs, reach_p0_offset + i);
            oppo_sum += r;
            card_sum[range_cards_0[i].card1] += r;
            card_sum[range_cards_0[i].card2] += r;
        }

        for (int t = threadIdx.x; t < range2_size; t += blockDim.x) {
            int my_card1 = range_cards_1[t].card1;
            int my_card2 = range_cards_1[t].card2;
            float effective_oppo = oppo_sum - card_sum[my_card1] - card_sum[my_card2];

            int same_hand_idx = p1_to_p0_map[t];
            if (same_hand_idx >= 0) {
                effective_oppo += read_val(reach_probs, reach_p0_offset + same_hand_idx);
            }

            write_val(leaf_utilities, util_p1_offset + t, (unpack_player(state.packed) == 1) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo));
        }
    }


}



// ----------------------------------------------------------------------------

// BACKWARD PASS

// ----------------------------------------------------------------------------

template<typename T>
__global__ void backward_pass_kernel(
    const GpuState* states, int num_states, int level_start, int level_count,
    T* regrets, T* cum_strategies,
    const T* reach_probs, const T* leaf_utilities, const T* utilities_in, T* utilities_out, int next_level_start,
    int range1_size, int range2_size, const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1, const float* dcfr_coeffs,
    const uint8_t* locked
) {
    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;
    int node_in_level = blockIdx.x;
    if (node_in_level >= level_count) return;

    int t = blockIdx.y * blockDim.x + threadIdx.x;
    if (t >= max_range_size) return;

    int node_data_size = range1_size + range2_size;
    int state_idx = level_start + node_in_level;

    float alpha_coef = dcfr_coeffs[0];
    float strategy_coef = dcfr_coeffs[1];

    const GpuState& state = states[state_idx];

    int p0_offset = node_in_level * node_data_size;
    int p1_offset = node_in_level * node_data_size + range1_size;



        if (unpack_node_type(state.packed) == 0) { // ACTION node

            int action_count = unpack_action_count(state.packed);

            int offset = state.trainable_offset;



            // P0 Processing

            if (t < range1_size) {

                float state_utility = 0.0f;

                for (int a = 0; a < action_count; ++a) {

                    float strat = 0.0f;
                    if (unpack_player(state.packed) == 0) {
                        if (is_locked(locked, offset + t)) {
                            strat = read_val(cum_strategies, offset + a * range1_size + t);
                        } else {
                            strat = compute_strategy_prob(regrets, offset, range1_size, t, action_count, a);
                        }
                    }

                    // Passive player assumes strategy 1.0 down the path they were forced into

                    if (unpack_player(state.packed) != 0) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    float child_util = 0.0f;
            int child_type = unpack_node_type(states[child_idx].packed);
            if (child_type == 1 || child_type == 2) {
                child_util = read_val(leaf_utilities, states[child_idx].child_start_idx * node_data_size + t);
            } else {
                child_util = read_val(utilities_in, (child_idx - next_level_start) * node_data_size + t);
            }
            state_utility += strat * child_util;

                }

                write_val(utilities_out, p0_offset + t, state_utility);



                if (unpack_player(state.packed) == 0) { // Update Regrets for P0
                    
                    if (!is_locked(locked, offset + t)) {

                        for (int a = 0; a < action_count; ++a) {

                            int child_idx = state.child_start_idx + a;

                            float child_util = 0.0f;
                            int child_type = unpack_node_type(states[child_idx].packed);
                            if (child_type == 1 || child_type == 2) {
                                child_util = read_val(leaf_utilities, states[child_idx].child_start_idx * node_data_size + t);
                            } else {
                                child_util = read_val(utilities_in, (child_idx - next_level_start) * node_data_size + t);
                            }

                            int idx = offset + a * range1_size + t;



                            float r_plus_val = read_val(regrets, idx);

                            r_plus_val += (child_util - state_utility);



                            // DCFR: damp regrets
                            if (r_plus_val > 0.0f) {
                                r_plus_val *= alpha_coef;
                            } else {
                                r_plus_val *= 0.5f; // beta = 0.5
                            }

                            write_val(regrets, idx, r_plus_val);



                            // DCFR: damp and accumulate average strategy
                            float cum_strat = read_val(cum_strategies, idx);

                            float strat;
                            if (is_locked(locked, offset + t)) {
                                strat = read_val(cum_strategies, idx);
                            } else {
                                strat = compute_strategy_prob(regrets, offset, range1_size, t, action_count, a);
                            }

                            cum_strat = cum_strat * 0.9f + strat * strategy_coef; // theta = 0.9

                            write_val(cum_strategies, idx, cum_strat);

                        }
                    }

                }

            }



            // P1 Processing

            if (t < range2_size) {

                float state_utility = 0.0f;

                for (int a = 0; a < action_count; ++a) {

                    float strat = 0.0f;
                    if (unpack_player(state.packed) == 1) {
                        if (is_locked(locked, offset + t)) {
                            strat = read_val(cum_strategies, offset + a * range2_size + t);
                        } else {
                            strat = compute_strategy_prob(regrets, offset, range2_size, t, action_count, a);
                        }
                    }

                    if (unpack_player(state.packed) != 1) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    float child_util = 0.0f;
            int child_type = unpack_node_type(states[child_idx].packed);
            if (child_type == 1 || child_type == 2) {
                child_util = read_val(leaf_utilities, states[child_idx].child_start_idx * node_data_size + range1_size + t);
            } else {
                child_util = read_val(utilities_in, (child_idx - next_level_start) * node_data_size + range1_size + t);
            }
            state_utility += strat * child_util;

                }

                write_val(utilities_out, p1_offset + t, state_utility);



                if (unpack_player(state.packed) == 1) { // Update Regrets for P1

                    if (!is_locked(locked, offset + t)) {

                        for (int a = 0; a < action_count; ++a) {

                            int child_idx = state.child_start_idx + a;

                            float child_util = 0.0f;
                            int child_type = unpack_node_type(states[child_idx].packed);
                            if (child_type == 1 || child_type == 2) {
                                child_util = read_val(leaf_utilities, states[child_idx].child_start_idx * node_data_size + range1_size + t);
                            } else {
                                child_util = read_val(utilities_in, (child_idx - next_level_start) * node_data_size + range1_size + t);
                            }

                            int idx = offset + a * range2_size + t;



                            float r_plus_val = read_val(regrets, idx);

                            r_plus_val += (child_util - state_utility);



                            // DCFR: damp regrets
                            if (r_plus_val > 0.0f) {
                                r_plus_val *= alpha_coef;
                            } else {
                                r_plus_val *= 0.5f; // beta = 0.5
                            }

                            write_val(regrets, idx, r_plus_val);



                            // DCFR: damp and accumulate average strategy
                            float cum_strat = read_val(cum_strategies, idx);

                            float strat;
                            if (is_locked(locked, offset + t)) {
                                strat = read_val(cum_strategies, idx);
                            } else {
                                strat = compute_strategy_prob(regrets, offset, range2_size, t, action_count, a);
                            }

                            cum_strat = cum_strat * 0.9f + strat * strategy_coef; // theta = 0.9

                            write_val(cum_strategies, idx, cum_strat);

                        }

                    }

                }

            }



        } else if (unpack_node_type(state.packed) == 3) { // CHANCE node

            int child_idx = state.child_start_idx;

            float p0_util = 0.0f;

            float p1_util = 0.0f;



            for (int j = 0; j < unpack_action_count(state.packed); ++j) {

                const GpuState& child_state = states[child_idx + j];



                if (t < range1_size) {

                    int c1 = range_cards_0[t].card1;

                    int c2 = range_cards_0[t].card2;

                    if (c1 != unpack_turn_card(child_state.packed) && c2 != unpack_turn_card(child_state.packed) &&

                        c1 != unpack_river_card(child_state.packed) && c2 != unpack_river_card(child_state.packed)) {

                        int child_type = unpack_node_type(child_state.packed);
                        if (child_type == 1 || child_type == 2) {
                            p0_util += read_val(leaf_utilities, child_state.child_start_idx * node_data_size + t);
                        } else {
                            p0_util += read_val(utilities_in, (child_idx + j - next_level_start) * node_data_size + t);
                        }

                    }

                }

                if (t < range2_size) {

                    int c1 = range_cards_1[t].card1;

                    int c2 = range_cards_1[t].card2;

                    if (c1 != unpack_turn_card(child_state.packed) && c2 != unpack_turn_card(child_state.packed) &&

                        c1 != unpack_river_card(child_state.packed) && c2 != unpack_river_card(child_state.packed)) {

                        int child_type = unpack_node_type(child_state.packed);
                        if (child_type == 1 || child_type == 2) {
                            p1_util += read_val(leaf_utilities, child_state.child_start_idx * node_data_size + range1_size + t);
                        } else {
                            p1_util += read_val(utilities_in, (child_idx + j - next_level_start) * node_data_size + range1_size + t);
                        }

                    }

                }

            }



            if (t < range1_size) {

                write_val(utilities_out, p0_offset + t, p0_util);

            }

            if (t < range2_size) {

                write_val(utilities_out, p1_offset + t, p1_util);

            }

        }
}



// ----------------------------------------------------------------------------

// LAUNCHERS
extern "C" {

void hip_launch_forward_pass(
    const GpuState* states, int num_states, int level_start, int level_count, int next_level_start,
    void* regrets, const void* cum_strategies, const void* reach_probs_in, void* reach_probs_out,
    int range1_size, int range2_size, bool use_fp16,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1, const uint8_t* locked,
    hipStream_t stream
) {
    int m = (range1_size > range2_size) ? range1_size : range2_size;
    dim3 blocks(level_count, (m + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);

    if (use_fp16)
        hipLaunchKernelGGL(forward_pass_kernel<uint16_t>, blocks, threads, 0, stream,
            states, num_states, level_start, level_count, next_level_start,
            (uint16_t*)regrets, (const uint16_t*)cum_strategies, (const uint16_t*)reach_probs_in, (uint16_t*)reach_probs_out,
            range1_size, range2_size, range_cards_0, range_cards_1, locked);
    else
        hipLaunchKernelGGL(forward_pass_kernel<float>, blocks, threads, 0, stream,
            states, num_states, level_start, level_count, next_level_start,
            (float*)regrets, (const float*)cum_strategies, (const float*)reach_probs_in, (float*)reach_probs_out,
            range1_size, range2_size, range_cards_0, range_cards_1, locked);
}

void hip_launch_backward_pass(
    const GpuState* states, int num_states, int level_start, int level_count,
    void* regrets, void* cum_strategies,
    const void* reach_probs, const void* leaf_utilities, const void* utilities_in, void* utilities_out, int next_level_start,
    int range1_size, int range2_size,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    const float* dcfr_coeffs, bool use_fp16, const uint8_t* locked, hipStream_t stream
) {
    int m = (range1_size > range2_size) ? range1_size : range2_size;
    dim3 blocks(level_count, (m + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);
    if (use_fp16)
        hipLaunchKernelGGL(backward_pass_kernel<uint16_t>, blocks, threads, 0, stream,
            states, num_states, level_start, level_count,
            (uint16_t*)regrets, (uint16_t*)cum_strategies,
            (const uint16_t*)reach_probs, (const uint16_t*)leaf_utilities, (const uint16_t*)utilities_in, (uint16_t*)utilities_out, next_level_start,
            range1_size, range2_size, range_cards_0, range_cards_1, dcfr_coeffs, locked);
    else
        hipLaunchKernelGGL(backward_pass_kernel<float>, blocks, threads, 0, stream,
            states, num_states, level_start, level_count,
            (float*)regrets, (float*)cum_strategies,
            (const float*)reach_probs, (const float*)leaf_utilities, (const float*)utilities_in, (float*)utilities_out, next_level_start,
            range1_size, range2_size, range_cards_0, range_cards_1, dcfr_coeffs, locked);
}

void hip_launch_showdown_pass(
    const GpuState* states, int n_sd,
    const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,
    const void* reach_probs, void* leaf_utilities,
    int range1_size, int range2_size,
    float win_payoff, float lose_payoff, int level_start, bool use_fp16, hipStream_t stream
) {
    dim3 blocks(n_sd);
    if (use_fp16) hipLaunchKernelGGL(showdown_pass_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, n_sd, showdowns, combs, (const uint16_t*)reach_probs, (uint16_t*)leaf_utilities, range1_size, range2_size, win_payoff, lose_payoff, level_start); else hipLaunchKernelGGL(showdown_pass_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, n_sd, showdowns, combs, (const float*)reach_probs, (float*)leaf_utilities, range1_size, range2_size, win_payoff, lose_payoff, level_start);
}

void hip_launch_terminal_pass(
    const GpuState* states, int num_states, int level_count, int level_start,
    const void* reach_probs, void* leaf_utilities,
    int range1_size, int range2_size,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    const int* p0_to_p1_map, const int* p1_to_p0_map, bool use_fp16,
    hipStream_t stream
) {
    dim3 blocks(level_count);
    if (use_fp16) hipLaunchKernelGGL(eval_terminal_nodes_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, (const uint16_t*)reach_probs, (uint16_t*)leaf_utilities, range1_size, range2_size, range_cards_0, range_cards_1, p0_to_p1_map, p1_to_p0_map); else hipLaunchKernelGGL(eval_terminal_nodes_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, (const float*)reach_probs, (float*)leaf_utilities, range1_size, range2_size, range_cards_0, range_cards_1, p0_to_p1_map, p1_to_p0_map);
}

}
