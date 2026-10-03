// Inherited methods dumps, get_strategy, get_evs, get_trainable from PCfrSolver are used directly.

#include <cuda_runtime.h>

#include <cuda_fp16.h>

#include <include/solver/GpuTypes.h>

__device__ inline bool is_locked(const uint8_t* locked_bits, int idx) {
    return (locked_bits[idx >> 3] >> (idx & 7)) & 1;
}


#include <iostream>

#include <cmath>

#include <stdint.h>
#include <stdio.h>
#ifndef cudaLaunchKernelGGL
#define cudaLaunchKernelGGL(kernel, grid, block, sharedMem, stream, ...) \
    do { \
        kernel<<<(grid), (block), (sharedMem), (stream)>>>(__VA_ARGS__); \
        cudaError_t err = cudaGetLastError(); \
        if (err != cudaSuccess) { \
            printf("Kernel Launch Error (%s): %s\n", #kernel, cudaGetErrorString(err)); \
        } \
    } while(0)
#endif



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

bool cuda_initialize_device() { cudaError_t err = cudaSetDevice(0); return err == cudaSuccess; }

void* cuda_allocate_device(size_t size) { 
    void* ptr = nullptr; 
    cudaError_t err = cudaMallocManaged(&ptr, size); 
    if (err != cudaSuccess) {
        printf("GPU Memory Allocation Failed for %zu bytes! Error %d\n", size, (int)err);
    }
    return ptr; 
}

void cuda_free_device(void* ptr) { if (ptr) cudaFree(ptr); }

void cuda_copy_to_device(void* dst, const void* src, size_t size) { cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice); }

void cuda_copy_to_host(void* dst, const void* src, size_t size) { cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost); }

void cuda_memset_device(void* ptr, int value, size_t size) { cudaMemset(ptr, value, size); }

}



// ----------------------------------------------------------------------------

// FORWARD PASS

// ----------------------------------------------------------------------------

template<typename T>
__global__ void forward_pass_kernel(
    const GpuState* states, int num_states, int level_start, int level_count,
    T* regrets, T* strategies, float* reach_probs,
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



            // 1. Calc Strategy ONLY for the active player

            if ((unpack_player(state.packed) == 0 && t < range1_size) || (unpack_player(state.packed) == 1 && t < range2_size)) {

                if (!is_locked(locked, offset + t)) {
                    float r_sum = 0.0f;

                    for (int a = 0; a < action_count; ++a) {

                        int idx = offset + a * active_range + t;

                        float r = read_val(regrets, idx);

                        if (r > 0.0f) r_sum += r;

                    }

                    for (int a = 0; a < action_count; ++a) {

                        int idx = offset + a * active_range + t;

                        float strategy_prob = 1.0f / action_count;

                        if (r_sum > 0.0f) {

                            float r = read_val(regrets, idx);

                            strategy_prob = (r > 0.0f) ? (r / r_sum) : 0.0f;

                        }

                        write_val(strategies, idx, strategy_prob);

                    }
                }

            }

            __syncthreads(); // Ensure all strategies are written before pushing reach down



            // 2. Propagate reach for BOTH players

            for (int a = 0; a < action_count; ++a) {

                int child_idx = state.child_start_idx + a;

                if (child_idx < num_states) {

                    const GpuState& child_state = states[child_idx];

                    int p0_parent = state_idx * node_data_size + t;

                    int p0_child  = child_idx * node_data_size + t;

                    int p1_parent = state_idx * node_data_size + range1_size + t;

                    int p1_child  = child_idx * node_data_size + range1_size + t;



                    if (t < range1_size) {

                        int card1 = range_cards_0[t].card1;

                        int card2 = range_cards_0[t].card2;

                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||

                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {

                            read_val(reach_probs, p0_child) = 0.0f;

                        } else {

                            if (unpack_player(state.packed) == 0) {

                                float strat = read_val(strategies, offset + a * range1_size + t);

                                read_val(reach_probs, p0_child) = read_val(reach_probs, p0_parent) * strat;

                            } else {

                                read_val(reach_probs, p0_child) = read_val(reach_probs, p0_parent); // Passive copy

                            }

                        }

                    }

                    if (t < range2_size) {

                        int card1 = range_cards_1[t].card1;

                        int card2 = range_cards_1[t].card2;

                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||

                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {

                            read_val(reach_probs, p1_child) = 0.0f;

                        } else {

                            if (unpack_player(state.packed) == 1) {

                                float strat = read_val(strategies, offset + a * range2_size + t);

                                read_val(reach_probs, p1_child) = read_val(reach_probs, p1_parent) * strat;

                            } else {

                                read_val(reach_probs, p1_child) = read_val(reach_probs, p1_parent); // Passive copy

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

                    if (t < range1_size) {

                        int card1 = range_cards_0[t].card1;

                        int card2 = range_cards_0[t].card2;

                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||

                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {

                            read_val(reach_probs, child_idx * node_data_size + t) = 0.0f;

                        } else {

                            read_val(reach_probs, child_idx * node_data_size + t) = read_val(reach_probs, state_idx * node_data_size + t) * scale;

                        }

                    }

                    if (t < range2_size) {

                        int card1 = range_cards_1[t].card1;

                        int card2 = range_cards_1[t].card2;

                        if (card1 == unpack_turn_card(child_state.packed) || card2 == unpack_turn_card(child_state.packed) ||

                            card1 == unpack_river_card(child_state.packed) || card2 == unpack_river_card(child_state.packed)) {

                            read_val(reach_probs, child_idx * node_data_size + range1_size + t) = 0.0f;

                        } else {

                            read_val(reach_probs, child_idx * node_data_size + range1_size + t) = read_val(reach_probs, state_idx * node_data_size + range1_size + t) * scale;

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

    const float* reach_probs, float* utilities,

    int range1_size, int range2_size, float win_payoff, float lose_payoff
) {

    int sd_idx = blockIdx.x;

    if (sd_idx >= n_sd) return;

    const ShowdownPrecalc& sd = showdowns[sd_idx];

    int state_idx = sd.state_idx;


    int node_data_size = range1_size + range2_size;
    int p0_offset = state_idx * node_data_size;
    int p1_offset = state_idx * node_data_size + range1_size;

    // OOP (Player 0) Evaluation
    for (int t = threadIdx.x; t < sd.player_size; t += blockDim.x) {
        const GpuShowdownComb& p_comb = combs[sd.player_comb_start + t];
        float win_sum = 0.0f, lose_sum = 0.0f;
        for (int j = 0; j < sd.oppo_size; ++j) {
            const GpuShowdownComb& o_comb = combs[sd.oppo_comb_start + j];
            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||
                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;
            float oppo_reach = read_val(reach_probs, p1_offset + o_comb.reach_prob_index);

            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;
            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;
            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }
        }
        utilities[p0_offset + p_comb.reach_prob_index] = ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f);
    }

    // IP (Player 1) Evaluation (Mirrored)
    for (int t = threadIdx.x; t < sd.oppo_size; t += blockDim.x) {
        const GpuShowdownComb& p_comb = combs[sd.oppo_comb_start + t];
        float win_sum = 0.0f, lose_sum = 0.0f;
        for (int j = 0; j < sd.player_size; ++j) {
            const GpuShowdownComb& o_comb = combs[sd.player_comb_start + j];
            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||
                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;
            float oppo_reach = read_val(reach_probs, p0_offset + o_comb.reach_prob_index);

            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;
            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;
            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }
        }
        utilities[p1_offset + p_comb.reach_prob_index] = ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f);
    }


}



// ----------------------------------------------------------------------------

// TERMINAL PASS (Both players calculated simultaneously)

// ----------------------------------------------------------------------------

template<typename T>
__global__ void eval_terminal_nodes_kernel(

    const GpuState* states, int num_states,

    const float* reach_probs, float* utilities,

    int range1_size, int range2_size,

    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,

    const int* p0_to_p1_map, const int* p1_to_p0_map
) {

    int state_idx = blockIdx.x;

    if (state_idx >= num_states) return;



    const GpuState& state = states[state_idx];

    if (unpack_node_type(state.packed) != 2) return;



    int node_data_size = range1_size + range2_size;
    int p0_offset = state_idx * node_data_size;
    int p1_offset = state_idx * node_data_size + range1_size;
    float fold_payoff = state.pot;

    // --- OOP (Player 0) Evaluation ---
    {
        float oppo_sum = 0.0f;
        float card_sum[52];
        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;

        for (int i = 0; i < range2_size; ++i) {
            float r = read_val(reach_probs, p1_offset + i);
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
                effective_oppo += read_val(reach_probs, p1_offset + same_hand_idx);
            }

            utilities[p0_offset + t] = (unpack_player(state.packed) == 0) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);
        }
    }

    // --- IP (Player 1) Evaluation ---
    {
        float oppo_sum = 0.0f;
        float card_sum[52];
        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;

        for (int i = 0; i < range1_size; ++i) {
            float r = read_val(reach_probs, p0_offset + i);
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
                effective_oppo += read_val(reach_probs, p0_offset + same_hand_idx);
            }

            utilities[p1_offset + t] = (unpack_player(state.packed) == 1) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);
        }
    }


}



// ----------------------------------------------------------------------------

// BACKWARD PASS

// ----------------------------------------------------------------------------

template<typename T>
__global__ void backward_pass_kernel(
    const GpuState* states, int num_states, int level_start, int level_count,
    T* regrets, T* strategies, T* cum_strategies,
    const float* reach_probs, float* utilities,
    int range1_size, int range2_size, const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1, int iter,
    const uint8_t* locked
) {
    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

    int node_in_level = blockIdx.x;
    if (node_in_level >= level_count) return;

    int t = blockIdx.y * blockDim.x + threadIdx.x;
    if (t >= max_range_size) return;

    int node_data_size = range1_size + range2_size;

    // Discounted CFR (DCFR) coefficients: alpha = 1.5, beta = 0.5, gamma = 2.0, theta = 0.9
    float f_iter = (float)(iter + 1);
    float alpha_coef = powf(f_iter, 1.5f);
    alpha_coef = alpha_coef / (1.0f + alpha_coef);
    float strategy_coef = powf(f_iter / (f_iter + 1.0f), 2.0f);

    int state_idx = level_start + node_in_level;

    const GpuState& state = states[state_idx];

        int p0_offset = state_idx * node_data_size;

        int p1_offset = state_idx * node_data_size + range1_size;



        if (unpack_node_type(state.packed) == 0) { // ACTION node

            int action_count = unpack_action_count(state.packed);

            int offset = state.trainable_offset;



            // P0 Processing

            if (t < range1_size) {

                float state_utility = 0.0f;

                for (int a = 0; a < action_count; ++a) {

                    float strat = (unpack_player(state.packed) == 0) ? read_val(strategies, offset + a * range1_size + t) : 0.0f;

                    // Passive player assumes strategy 1.0 down the path they were forced into

                    if (unpack_player(state.packed) != 0) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    state_utility += strat * utilities[child_idx * node_data_size + t];

                }

                utilities[p0_offset + t] = state_utility;



                if (unpack_player(state.packed) == 0) { // Update Regrets for P0
                    
                    if (!is_locked(locked, offset + t)) {

                        for (int a = 0; a < action_count; ++a) {

                            int child_idx = state.child_start_idx + a;

                            float child_util = utilities[child_idx * node_data_size + t];

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

                            float strat = read_val(strategies, idx);

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

                    float strat = (unpack_player(state.packed) == 1) ? read_val(strategies, offset + a * range2_size + t) : 0.0f;

                    if (unpack_player(state.packed) != 1) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    state_utility += strat * utilities[child_idx * node_data_size + range1_size + t];

                }

                utilities[p1_offset + t] = state_utility;



                if (unpack_player(state.packed) == 1) { // Update Regrets for P1

                    if (!is_locked(locked, offset + t)) {

                        for (int a = 0; a < action_count; ++a) {

                            int child_idx = state.child_start_idx + a;

                            float child_util = utilities[child_idx * node_data_size + range1_size + t];

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

                            float strat = read_val(strategies, idx);

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

                        p0_util += utilities[(child_idx + j) * node_data_size + t];

                    }

                }

                if (t < range2_size) {

                    int c1 = range_cards_1[t].card1;

                    int c2 = range_cards_1[t].card2;

                    if (c1 != unpack_turn_card(child_state.packed) && c2 != unpack_turn_card(child_state.packed) &&

                        c1 != unpack_river_card(child_state.packed) && c2 != unpack_river_card(child_state.packed)) {

                        p1_util += utilities[(child_idx + j) * node_data_size + range1_size + t];

                    }

                }

            }



            if (t < range1_size) {

                utilities[p0_offset + t] = p0_util;

            }

            if (t < range2_size) {

                utilities[p1_offset + t] = p1_util;

        }

    }

}



// ----------------------------------------------------------------------------

// LAUNCHERS

// ----------------------------------------------------------------------------

extern "C" {

void cuda_launch_forward_pass(
    const GpuState* states, int num_states, int level_start, int level_count,
    void* regrets, void* strategies, float* reach_probs,
    int range1_size, int range2_size, bool use_fp16,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    const uint8_t* locked, cudaStream_t stream
) {
    int m = (range1_size > range2_size) ? range1_size : range2_size;
    dim3 blocks(level_count, (m + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (use_fp16) cudaLaunchKernelGGL(forward_pass_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, level_count, (uint16_t*)regrets, (uint16_t*)strategies, reach_probs, range1_size, range2_size, range_cards_0, range_cards_1, locked);
    else cudaLaunchKernelGGL(forward_pass_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, level_count, (float*)regrets, (float*)strategies, reach_probs, range1_size, range2_size, range_cards_0, range_cards_1, locked);
}

void cuda_launch_backward_pass(
    const GpuState* states, int num_states, int level_start, int level_count,
    void* regrets, void* strategies, void* cum_strategies,
    float* reach_probs, float* utilities, int range1_size, int range2_size, const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1, int iter, bool use_fp16, const uint8_t* locked, cudaStream_t stream
) {
    int m = (range1_size > range2_size) ? range1_size : range2_size;
    dim3 blocks(level_count, (m + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (use_fp16) cudaLaunchKernelGGL(backward_pass_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, level_count, (uint16_t*)regrets, (uint16_t*)strategies, (uint16_t*)cum_strategies, reach_probs, utilities, range1_size, range2_size, range_cards_0, range_cards_1, iter, locked);
    else cudaLaunchKernelGGL(backward_pass_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, num_states, level_start, level_count, (float*)regrets, (float*)strategies, (float*)cum_strategies, reach_probs, utilities, range1_size, range2_size, range_cards_0, range_cards_1, iter, locked);
}



void cuda_launch_showdown_pass(

    const GpuState* states, int n_sd, const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,

    float* reach_probs, float* utilities, int range1_size, int range2_size, float win_payoff, float lose_payoff, cudaStream_t stream

) {

    dim3 blocks(n_sd);

    cudaLaunchKernelGGL(showdown_pass_kernel, blocks, BLOCK_SIZE, 0, stream, states, n_sd, showdowns, combs, reach_probs, utilities, range1_size, range2_size, win_payoff, lose_payoff);

}



void cuda_launch_terminal_pass(

    const GpuState* states, int num_states, float* reach_probs, float* utilities, int range1_size, int range2_size,

    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,

    const int* p0_to_p1_map, const int* p1_to_p0_map, bool use_fp16,

    cudaStream_t stream

) {

    dim3 blocks(num_states);

    cudaLaunchKernelGGL(terminal_pass_kernel, blocks, BLOCK_SIZE, 0, stream, states, num_states, reach_probs, utilities, range1_size, range2_size, range_cards_0, range_cards_1, p0_to_p1_map, p1_to_p0_map);

}

}
