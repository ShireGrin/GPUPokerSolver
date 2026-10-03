#include "include/preflop/PreflopIcmCalculator.h"
#include <numeric>

PreflopIcmCalculator::PreflopIcmCalculator() {
    // Default to empty payouts (Chip EV mode)
}

PreflopIcmCalculator::PreflopIcmCalculator(const vector<float>& payouts) {
    this->payouts = payouts;
}

vector<float> PreflopIcmCalculator::calculate_equities(const vector<float>& stacks) {
    int n = stacks.size();
    vector<float> equities(n, 0.0f);

    if (payouts.empty()) {
        // Chip EV Mode: Equity is directly stacks in chips (BB)
        return stacks;
    }

    // Tournament ICM Mode: Malmuth-Harville recursion
    vector<bool> in_play(n, true);
    float sum_stacks = std::accumulate(stacks.begin(), stacks.end(), 0.0f);

    icm_dfs(stacks, in_play, sum_stacks, 0, 1.0f, equities);
    
    return equities;
}

void PreflopIcmCalculator::icm_dfs(
    const vector<float>& stacks,
    vector<bool>& in_play,
    float sum_stacks,
    int finish_position,
    float path_prob,
    vector<float>& expected_payouts
) {
    if (finish_position >= payouts.size() || path_prob < 1e-7 || sum_stacks < 1e-5) {
        return;
    }

    float payout = payouts[finish_position];
    int n = stacks.size();

    for (int i = 0; i < n; ++i) {
        if (in_play[i] && stacks[i] > 0.0f) {
            float prob = stacks[i] / sum_stacks;
            float step_prob = path_prob * prob;

            expected_payouts[i] += step_prob * payout;

            in_play[i] = false;
            icm_dfs(stacks, in_play, sum_stacks - stacks[i], finish_position + 1, step_prob, expected_payouts);
            in_play[i] = true;
        }
    }
}
