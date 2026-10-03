#pragma once

// Flat GPU representation of a game tree state (node + board deal combination)
struct GpuState {
    int child_start_idx;   // index of first child state in the global GpuState array
    int trainable_offset;  // offset in the global regrets/strategies array (-1 if none)
    int range_size;        // number of hand combos for active player
    float pot;             // current pot size
    int deal_id;           // deal ID
    int parent_idx;        // parent state index
    int action_idx_in_parent; // which action index in parent this state corresponds to
    uint32_t packed;       // node_type(2) | player(2) | action_count(8) | turn_card(7) | river_card(7) | padding(6)
};

__device__ inline int unpack_node_type(uint32_t packed) { return packed & 0x3; }
__device__ inline int unpack_player(uint32_t packed) { return (int)((packed >> 2) & 0x3) - 1; }
__device__ inline int unpack_action_count(uint32_t packed) { return (packed >> 4) & 0xFF; }
__device__ inline int unpack_turn_card(uint32_t packed) { int v = (packed >> 12) & 0x7F; return v == 127 ? -1 : v; }
__device__ inline int unpack_river_card(uint32_t packed) { int v = (packed >> 19) & 0x7F; return v == 127 ? -1 : v; }


// Precomputed River Showdown card-ranking details for binary search lookup on GPU
struct GpuShowdownComb {
    int reach_prob_index;  // index in range
    int rank;              // hand strength rank
    int card1;             // first card index (0-51)
    int card2;             // second card index (0-51)
};

// Precomputed River Showdown metadata
struct ShowdownPrecalc {
    int state_idx;
    int player_size;
    int oppo_size;
    int player_comb_start; // index in global d_showdown_combs
    int oppo_comb_start;   // index in global d_showdown_combs
};

// Card data for each hand in a player's range, used for blocker calculations
struct GpuRangeCard {
    int card1;  // first card index (0-51)
    int card2;  // second card index (0-51)
};
