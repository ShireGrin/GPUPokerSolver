import re

with open("HipKernels_orig.hip", "r") as f:
    content = f.read()

# Fix terminal_pass_kernel
terminal_repl = """
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
            float r = reach_probs[p1_offset + i];
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
                effective_oppo += reach_probs[p1_offset + same_hand_idx];
            }

            utilities[p0_offset + t] = (state.player == 0) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);
        }
    }

    // --- IP (Player 1) Evaluation ---
    {
        float oppo_sum = 0.0f;
        float card_sum[52];
        for (int c = 0; c < 52; ++c) card_sum[c] = 0.0f;

        for (int i = 0; i < range1_size; ++i) {
            float r = reach_probs[p0_offset + i];
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
                effective_oppo += reach_probs[p0_offset + same_hand_idx];
            }

            utilities[p1_offset + t] = (state.player == 1) ? (-fold_payoff * effective_oppo) : (fold_payoff * effective_oppo);
        }
    }

    if (threadIdx.x == 0) debug_flags[state_idx] = 1;
}
"""

content = re.sub(r'    int max_range_size = \(range1_size > range2_size\).*?if \(t == 0\) debug_flags\[state_idx\] = 1;\n\n}', terminal_repl, content, flags=re.DOTALL)

# Fix showdown_pass_kernel
showdown_repl = """
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
            float oppo_reach = reach_probs[p1_offset + o_comb.reach_prob_index];

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
            float oppo_reach = reach_probs[p0_offset + o_comb.reach_prob_index];

            if (p_comb.rank < o_comb.rank) win_sum += oppo_reach;
            else if (p_comb.rank > o_comb.rank) lose_sum += oppo_reach;
            else { float tie = oppo_reach * 0.5f; win_sum += tie; lose_sum += tie; }
        }
        utilities[p1_offset + p_comb.reach_prob_index] = ((win_sum * win_payoff) + (lose_sum * lose_payoff)) * (states[state_idx].pot * 0.5f);
    }

    if (threadIdx.x == 0) debug_flags[state_idx] = 1;
}
"""

content = re.sub(r'    int max_range_size = \(range1_size > range2_size\).*?if \(t == 0\) debug_flags\[state_idx\] = 1;\n\n}(?=\n\n\n// ---)', showdown_repl, content, flags=re.DOTALL)

with open("HipKernels.hip", "w") as f:
    f.write(content)
