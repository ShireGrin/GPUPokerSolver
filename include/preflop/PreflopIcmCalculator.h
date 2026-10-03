#ifndef TEXASSOLVER_PREFLOPICMCALCULATOR_H
#define TEXASSOLVER_PREFLOPICMCALCULATOR_H

#include <vector>

using namespace std;

class PreflopIcmCalculator {
private:
    vector<float> payouts; // Payout vector (e.g. [0.5, 0.3, 0.2])

    // Recursive helper to calculate Malmuth-Harville probabilities
    void icm_dfs(
        const vector<float>& stacks,
        vector<bool>& in_play,
        float sum_stacks,
        int finish_position,
        float path_prob,
        vector<float>& expected_payouts
    );

public:
    PreflopIcmCalculator();
    PreflopIcmCalculator(const vector<float>& payouts);

    // Calculates the cash equity of each player based on their stack size
    vector<float> calculate_equities(const vector<float>& stacks);
    const vector<float>& get_payouts() const { return payouts; }
};

#endif // TEXASSOLVER_PREFLOPICMCALCULATOR_H
