// Inherited methods dumps, get_strategy, get_evs, get_trainable from PCfrSolver are used directly.

#include <hip/hip_runtime.h>

#include <hip/hip_fp16.h>

#include <include/solver/GpuTypes.h>

#include <iostream>

#include <cmath>



#define BLOCK_SIZE 256



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

void* hip_allocate_device(size_t size) { void* ptr = nullptr; hipMalloc(&ptr, size); return ptr; }

void hip_free_device(void* ptr) { if (ptr) hipFree(ptr); }

void hip_copy_to_device(void* dst, const void* src, size_t size) { hipMemcpy(dst, src, size, hipMemcpyHostToDevice); }

void hip_copy_to_host(void* dst, const void* src, size_t size) { hipMemcpy(dst, src, size, hipMemcpyDeviceToHost); }

void hip_memset_device(void* ptr, int value, size_t size) { hipMemset(ptr, value, size); }

}



// ----------------------------------------------------------------------------

// FORWARD PASS

// ----------------------------------------------------------------------------

template<typename T>

__global__ void forward_pass_kernel(

    const GpuState* states, int num_states,

    T* regrets, T* strategies, float* reach_probs,

    int range1_size, int range2_size,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1

) {

    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

    int t = blockIdx.x * blockDim.x + threadIdx.x;

    if (t >= max_range_size) return;



    int node_data_size = range1_size + range2_size;



    for (int state_idx = 0; state_idx < num_states; ++state_idx) {

        const GpuState& state = states[state_idx];



        if (state.node_type == 0) { // ACTION node

            int action_count = state.action_count;

            int offset = state.trainable_offset;

            int active_range = (state.player == 0) ? range1_size : range2_size;



            // 1. Calc Strategy ONLY for the active player

            if ((state.player == 0 && t < range1_size) || (state.player == 1 && t < range2_size)) {

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

                        if (card1 == child_state.turn_card || card2 == child_state.turn_card ||

                            card1 == child_state.river_card || card2 == child_state.river_card) {

                            reach_probs[p0_child] = 0.0f;

                        } else {

                            if (state.player == 0) {

                                float strat = read_val(strategies, offset + a * range1_size + t);

                                reach_probs[p0_child] = reach_probs[p0_parent] * strat;

                            } else {

                                reach_probs[p0_child] = reach_probs[p0_parent]; // Passive copy

                            }

                        }

                    }

                    if (t < range2_size) {

                        int card1 = range_cards_1[t].card1;

                        int card2 = range_cards_1[t].card2;

                        if (card1 == child_state.turn_card || card2 == child_state.turn_card ||

                            card1 == child_state.river_card || card2 == child_state.river_card) {

                            reach_probs[p1_child] = 0.0f;

                        } else {

                            if (state.player == 1) {

                                float strat = read_val(strategies, offset + a * range2_size + t);

                                reach_probs[p1_child] = reach_probs[p1_parent] * strat;

                            } else {

                                reach_probs[p1_child] = reach_probs[p1_parent]; // Passive copy

                            }

                        }

                    }

                }

            }

        } else if (state.node_type == 3) { // CHANCE node

            float scale = 1.0f / (float)(state.action_count - 2);

            for (int c = 0; c < state.action_count; ++c) {

                int child_idx = state.child_start_idx + c;

                if (child_idx < num_states) {

                    const GpuState& child_state = states[child_idx];

                    if (t < range1_size) {

                        int card1 = range_cards_0[t].card1;

                        int card2 = range_cards_0[t].card2;

                        if (card1 == child_state.turn_card || card2 == child_state.turn_card ||

                            card1 == child_state.river_card || card2 == child_state.river_card) {

                            reach_probs[child_idx * node_data_size + t] = 0.0f;

                        } else {

                            reach_probs[child_idx * node_data_size + t] = reach_probs[state_idx * node_data_size + t] * scale;

                        }

                    }

                    if (t < range2_size) {

                        int card1 = range_cards_1[t].card1;

                        int card2 = range_cards_1[t].card2;

                        if (card1 == child_state.turn_card || card2 == child_state.turn_card ||

                            card1 == child_state.river_card || card2 == child_state.river_card) {

                            reach_probs[child_idx * node_data_size + range1_size + t] = 0.0f;

                        } else {

                            reach_probs[child_idx * node_data_size + range1_size + t] = reach_probs[state_idx * node_data_size + range1_size + t] * scale;

                        }

                    }

                }

            }

        }

        __syncthreads();

    }

}



// ----------------------------------------------------------------------------

// SHOWDOWN PASS (Both players calculated simultaneously)

// ----------------------------------------------------------------------------

__global__ void showdown_pass_kernel(

    const GpuState* states, int n_sd,

    const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,

    const float* reach_probs, float* utilities,

    int range1_size, int range2_size, float win_payoff, float lose_payoff, int* debug_flags

) {

    int sd_idx = blockIdx.x;

    if (sd_idx >= n_sd) return;

    const ShowdownPrecalc& sd = showdowns[sd_idx];

    int state_idx = sd.state_idx;


    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

    int t = threadIdx.x;

    if (t >= max_range_size) return;



    int node_data_size = range1_size + range2_size;

    int p0_offset = state_idx * node_data_size;

    int p1_offset = state_idx * node_data_size + range1_size;



    // OOP (Player 0) Evaluation

    if (t < range1_size) {

        const GpuShowdownComb& p_comb = combs[sd.player_comb_start + t];

        float win_sum = 0.0f, lose_sum = 0.0f;

        for (int j = 0; j < range2_size; ++j) {

            const GpuShowdownComb& o_comb = combs[sd.oppo_comb_start + j];

            // Card blocker: skip if opponent shares any card with player

            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||

                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;

            float oppo_reach = reach_probs[p1_offset + o_comb.reach_prob_index];



            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;

            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;

            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }

        }

        utilities[p0_offset + p_comb.reach_prob_index] = ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f);

    }



    // IP (Player 1) Evaluation (Mirrored)

    if (t < range2_size) {

        const GpuShowdownComb& p_comb = combs[sd.oppo_comb_start + t];

        float win_sum = 0.0f, lose_sum = 0.0f;

        for (int j = 0; j < range1_size; ++j) {

            const GpuShowdownComb& o_comb = combs[sd.player_comb_start + j];

            // Card blocker: skip if opponent shares any card with player

            if (p_comb.card1 == o_comb.card1 || p_comb.card1 == o_comb.card2 ||

                p_comb.card2 == o_comb.card1 || p_comb.card2 == o_comb.card2) continue;

            float oppo_reach = reach_probs[p0_offset + o_comb.reach_prob_index];



            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;

            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;

            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }

        }

        utilities[p1_offset + p_comb.reach_prob_index] = ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f);

    }



    if (t == 0) debug_flags[state_idx] = 1;

}



// ----------------------------------------------------------------------------

// TERMINAL PASS (Both players calculated simultaneously)

// ----------------------------------------------------------------------------

__global__ void terminal_pass_kernel(

    const GpuState* states, int num_states,

    const float* reach_probs, float* utilities,

    int range1_size, int range2_size,

    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,

    const int* p0_to_p1_map, const int* p1_to_p0_map,

    int* debug_flags

) {

    int state_idx = blockIdx.x;

    if (state_idx >= num_states) return;



    const GpuState& state = states[state_idx];

    if (state.node_type != 2) return;



    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

    int t = threadIdx.x;

    if (t >= max_range_size) return;



    int node_data_size = range1_size + range2_size;

    int p0_offset = state_idx * node_data_size;

    int p1_offset = state_idx * node_data_size + range1_size;



    float fold_payoff = state.pot;



    // P0 utility: fold_payoff * (oppo_sum - card_blocker + same_hand_correction)

    if (t < range1_size) {

        int my_card1 = range_cards_0[t].card1;

        int my_card2 = range_cards_0[t].card2;



        // Sum opponent (P1) reach probs, tracking per-card sums for blocker subtraction

        float oppo_sum = 0.0f;

        float card_sum[52];

        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;



        for (int i = 0; i < range2_size; ++i) {

            float r = reach_probs[p1_offset + i];

            oppo_sum += r;

            card_sum[range_cards_1[i].card1] += r;

            card_sum[range_cards_1[i].card2] += r;

        }



        // Subtract hands that share a card with my hand

        float effective_oppo = oppo_sum - card_sum[my_card1] - card_sum[my_card2];



        // Add back the same-hand reach (it was double-subtracted)

        int same_hand_idx = p0_to_p1_map[t];

        if (same_hand_idx >= 0) {

            effective_oppo += reach_probs[p1_offset + same_hand_idx];

        }



        utilities[p0_offset + t] = (state.player == 0) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);

    }



    // P1 utility: fold_payoff * (oppo_sum - card_blocker + same_hand_correction)

    if (t < range2_size) {

        int my_card1 = range_cards_1[t].card1;

        int my_card2 = range_cards_1[t].card2;



        float oppo_sum = 0.0f;

        float card_sum[52];

        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;



        for (int i = 0; i < range1_size; ++i) {

            float r = reach_probs[p0_offset + i];

            oppo_sum += r;

            card_sum[range_cards_0[i].card1] += r;

            card_sum[range_cards_0[i].card2] += r;

        }



        float effective_oppo = oppo_sum - card_sum[my_card1] - card_sum[my_card2];



        int same_hand_idx = p1_to_p0_map[t];

        if (same_hand_idx >= 0) {

            effective_oppo += reach_probs[p0_offset + same_hand_idx];

        }



        utilities[p1_offset + t] = (state.player == 1) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);

    }



    if (t == 0) debug_flags[state_idx] = 1;

}



// ----------------------------------------------------------------------------

// BACKWARD PASS

// ----------------------------------------------------------------------------

template<typename T>

__global__ void backward_pass_kernel(

    const GpuState* states, int num_states,

    T* regrets, T* strategies, T* cum_strategies,

    const float* reach_probs, float* utilities,

    int range1_size, int range2_size, int iter

) {

    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

    int t = blockIdx.x * blockDim.x + threadIdx.x;

    if (t >= max_range_size) return;



    int node_data_size = range1_size + range2_size;

    // Pure Regret Matching+ (RM+): no DCFR dampening coefficients



    for (int state_idx = num_states - 1; state_idx >= 0; --state_idx) {

        const GpuState& state = states[state_idx];

        int p0_offset = state_idx * node_data_size;

        int p1_offset = state_idx * node_data_size + range1_size;



        if (state.node_type == 0) { // ACTION node

            int action_count = state.action_count;

            int offset = state.trainable_offset;



            // P0 Processing

            if (t < range1_size) {

                float state_utility = 0.0f;

                for (int a = 0; a < action_count; ++a) {

                    float strat = (state.player == 0) ? read_val(strategies, offset + a * range1_size + t) : 0.0f;

                    // Passive player assumes strategy 1.0 down the path they were forced into

                    if (state.player != 0) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    state_utility += strat * utilities[child_idx * node_data_size + t];

                }

                utilities[p0_offset + t] = state_utility;



                if (state.player == 0) { // Update Regrets for P0

                    for (int a = 0; a < action_count; ++a) {

                        int child_idx = state.child_start_idx + a;

                        float child_util = utilities[child_idx * node_data_size + t];

                        int idx = offset + a * range1_size + t;



                        float r_plus_val = read_val(regrets, idx);

                        r_plus_val += (child_util - state_utility);

                        // RM+: clamp negative regrets to zero

                        if (r_plus_val < 0.0f) r_plus_val = 0.0f;

                        write_val(regrets, idx, r_plus_val);



                        // Pure additive cumulative strategy accumulation

                        float cum_strat = read_val(cum_strategies, idx);

                        float strat = read_val(strategies, idx);

                        cum_strat += strat;

                        write_val(cum_strategies, idx, cum_strat);

                    }

                }

            }



            // P1 Processing

            if (t < range2_size) {

                float state_utility = 0.0f;

                for (int a = 0; a < action_count; ++a) {

                    float strat = (state.player == 1) ? read_val(strategies, offset + a * range2_size + t) : 0.0f;

                    if (state.player != 1) strat = 1.0f;



                    int child_idx = state.child_start_idx + a;

                    state_utility += strat * utilities[child_idx * node_data_size + range1_size + t];

                }

                utilities[p1_offset + t] = state_utility;



                if (state.player == 1) { // Update Regrets for P1

                    for (int a = 0; a < action_count; ++a) {

                        int child_idx = state.child_start_idx + a;

                        float child_util = utilities[child_idx * node_data_size + range1_size + t];

                        int idx = offset + a * range2_size + t;



                        float r_plus_val = read_val(regrets, idx);

                        r_plus_val += (child_util - state_utility);

                        // RM+: clamp negative regrets to zero

                        if (r_plus_val < 0.0f) r_plus_val = 0.0f;

                        write_val(regrets, idx, r_plus_val);



                        // Pure additive cumulative strategy accumulation

                        float cum_strat = read_val(cum_strategies, idx);

                        float strat = read_val(strategies, idx);

                        cum_strat += strat;

                        write_val(cum_strategies, idx, cum_strat);

                    }

                }

            }

        } else if (state.node_type == 3) { // CHANCE node

            if (state.action_count > 0) {

                if (t < range1_size) {

                    float total_u = 0.0f;

                    for (int c = 0; c < state.action_count; ++c) total_u += utilities[(state.child_start_idx + c) * node_data_size + t];

                    utilities[p0_offset + t] = total_u;

                }

                if (t < range2_size) {

                    float total_u = 0.0f;

                    for (int c = 0; c < state.action_count; ++c) total_u += utilities[(state.child_start_idx + c) * node_data_size + range1_size + t];

                    utilities[p1_offset + t] = total_u;

                }

            }

        }

        __syncthreads();

    }

}



// ----------------------------------------------------------------------------

// LAUNCHERS

// ----------------------------------------------------------------------------

extern "C" {

void hip_launch_forward_pass(

    const GpuState* states, int num_states, void* regrets, void* strategies, float* reach_probs,

    int range1_size, int range2_size, bool use_fp16,
    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,
    hipStream_t stream

) {

    int m = (range1_size > range2_size) ? range1_size : range2_size;

    dim3 blocks((m + BLOCK_SIZE - 1) / BLOCK_SIZE);

    if (use_fp16) hipLaunchKernelGGL(forward_pass_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, num_states, (uint16_t*)regrets, (uint16_t*)strategies, reach_probs, range1_size, range2_size, range_cards_0, range_cards_1);

    else hipLaunchKernelGGL(forward_pass_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, num_states, (float*)regrets, (float*)strategies, reach_probs, range1_size, range2_size, range_cards_0, range_cards_1);

}



void hip_launch_backward_pass(

    const GpuState* states, int num_states, void* regrets, void* strategies, void* cum_strategies,

    float* reach_probs, float* utilities, int range1_size, int range2_size, int iter, bool use_fp16, hipStream_t stream

) {

    int m = (range1_size > range2_size) ? range1_size : range2_size;

    dim3 blocks((m + BLOCK_SIZE - 1) / BLOCK_SIZE);

    if (use_fp16) hipLaunchKernelGGL(backward_pass_kernel<uint16_t>, blocks, BLOCK_SIZE, 0, stream, states, num_states, (uint16_t*)regrets, (uint16_t*)strategies, (uint16_t*)cum_strategies, reach_probs, utilities, range1_size, range2_size, iter);

    else hipLaunchKernelGGL(backward_pass_kernel<float>, blocks, BLOCK_SIZE, 0, stream, states, num_states, (float*)regrets, (float*)strategies, (float*)cum_strategies, reach_probs, utilities, range1_size, range2_size, iter);

}



void hip_launch_showdown_pass(

    const GpuState* states, int n_sd, const ShowdownPrecalc* showdowns, const GpuShowdownComb* combs,

    float* reach_probs, float* utilities, int range1_size, int range2_size, float win_payoff, float lose_payoff, int* d_debug_flags, hipStream_t stream

) {

    dim3 blocks(n_sd);

    hipLaunchKernelGGL(showdown_pass_kernel, blocks, BLOCK_SIZE, 0, stream, states, n_sd, showdowns, combs, reach_probs, utilities, range1_size, range2_size, win_payoff, lose_payoff, d_debug_flags);

}



void hip_launch_terminal_pass(

    const GpuState* states, int num_states, float* reach_probs, float* utilities, int range1_size, int range2_size,

    const GpuRangeCard* range_cards_0, const GpuRangeCard* range_cards_1,

    const int* p0_to_p1_map, const int* p1_to_p0_map,

    int* d_debug_flags, hipStream_t stream

) {

    dim3 blocks(num_states);

    hipLaunchKernelGGL(terminal_pass_kernel, blocks, BLOCK_SIZE, 0, stream, states, num_states, reach_probs, utilities, range1_size, range2_size, range_cards_0, range_cards_1, p0_to_p1_map, p1_to_p0_map, d_debug_flags);

}

}
