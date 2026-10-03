with open("HipKernels_orig.hip", "r") as f:
    orig = f.read()

# For terminal_pass_kernel:
term_orig = """    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

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



    if (t == 0) debug_flags[state_idx] = 1;"""

term_new = """    int node_data_size = range1_size + range2_size;
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

    if (threadIdx.x == 0) debug_flags[state_idx] = 1;"""

orig = orig.replace(term_orig, term_new)

showdown_orig = """    int max_range_size = (range1_size > range2_size) ? range1_size : range2_size;

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



    if (t == 0) debug_flags[state_idx] = 1;"""

showdown_new = """    int node_data_size = range1_size + range2_size;
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

    if (threadIdx.x == 0) debug_flags[state_idx] = 1;"""

orig = orig.replace(showdown_orig, showdown_new)

with open("HipKernels.hip", "w") as f:
    f.write(orig)
