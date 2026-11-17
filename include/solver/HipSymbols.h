#pragma once
#include <hip/hip_runtime_api.h>   // for hipStream_t
#include <cstddef>
#include <cstdint>
#include "GpuTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif
