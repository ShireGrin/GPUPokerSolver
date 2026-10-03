#include "include/preflop/PreflopEquityManager.h"
#include <algorithm>
#include <random>

PreflopEquityManager::PreflopEquityManager(shared_ptr<Compairer> evaluator) {
    this->hand_evaluator = evaluator;
    load_default_eqr_profile();
}

string PreflopEquityManager::categorize_hand(const PrivateCards& hand) const {
    int r1 = hand.card1 % 13;
    int r2 = hand.card2 % 13;
    int s1 = hand.card1 / 13;
    int s2 = hand.card2 / 13;

    if (r1 == r2) return "pocket_pair";
    
    bool suited = (s1 == s2);
    int diff = abs(r1 - r2);
    bool connector = (diff == 1 || diff == 12); // Ace can connect with Deuce (0) and King (12)
    
    if (suited && connector) return "suited_connector";
    if (suited) return "suited";
    
    bool broadway = (r1 >= 8 && r2 >= 8); // 8 is Ten in TexasSolver rank index if 0 is Two
    if (broadway) return "offsuit_broadway";
    
    return "offsuit";
}

void PreflopEquityManager::load_default_eqr_profile() {
    eqr_profile.clear();

    // Default EQR profiles for standard positions
    // BTN (In-position, high EQR)
    eqr_profile["BTN"]["pocket_pair"] = 1.05f;
    eqr_profile["BTN"]["suited_connector"] = 1.10f;
    eqr_profile["BTN"]["suited"] = 1.05f;
    eqr_profile["BTN"]["offsuit_broadway"] = 1.02f;
    eqr_profile["BTN"]["offsuit"] = 0.98f;

    // BB (Defending, out-of-position, low EQR)
    eqr_profile["BB"]["pocket_pair"] = 0.90f;
    eqr_profile["BB"]["suited_connector"] = 0.85f;
    eqr_profile["BB"]["suited"] = 0.82f;
    eqr_profile["BB"]["offsuit_broadway"] = 0.80f;
    eqr_profile["BB"]["offsuit"] = 0.70f;

    // SB (Out-of-position, mid EQR)
    eqr_profile["SB"]["pocket_pair"] = 0.95f;
    eqr_profile["SB"]["suited_connector"] = 0.90f;
    eqr_profile["SB"]["suited"] = 0.88f;
    eqr_profile["SB"]["offsuit_broadway"] = 0.85f;
    eqr_profile["SB"]["offsuit"] = 0.75f;

    // Default IP/OOP fallbacks for other seats
    eqr_profile["IP_DEFAULT"]["pocket_pair"] = 1.02f;
    eqr_profile["IP_DEFAULT"]["suited_connector"] = 1.05f;
    eqr_profile["IP_DEFAULT"]["suited"] = 1.02f;
    eqr_profile["IP_DEFAULT"]["offsuit_broadway"] = 1.00f;
    eqr_profile["IP_DEFAULT"]["offsuit"] = 0.95f;

    eqr_profile["OOP_DEFAULT"]["pocket_pair"] = 0.98f;
    eqr_profile["OOP_DEFAULT"]["suited_connector"] = 0.95f;
    eqr_profile["OOP_DEFAULT"]["suited"] = 0.92f;
    eqr_profile["OOP_DEFAULT"]["offsuit_broadway"] = 0.90f;
    eqr_profile["OOP_DEFAULT"]["offsuit"] = 0.85f;
}

void PreflopEquityManager::load_eqr_profile(const string& json_path) {
    // Placeholder: JSON loading can be added later using json::parse
    (void)json_path;
}

void PreflopEquityManager::set_profile_type(int profile_type) {
    eqr_profile.clear();
    if (profile_type == 1) { // Tight
        // BTN
        eqr_profile["BTN"]["pocket_pair"] = 1.10f;
        eqr_profile["BTN"]["suited_connector"] = 1.12f;
        eqr_profile["BTN"]["suited"] = 1.08f;
        eqr_profile["BTN"]["offsuit_broadway"] = 1.00f;
        eqr_profile["BTN"]["offsuit"] = 0.90f;
        // BB
        eqr_profile["BB"]["pocket_pair"] = 0.95f;
        eqr_profile["BB"]["suited_connector"] = 0.80f;
        eqr_profile["BB"]["suited"] = 0.75f;
        eqr_profile["BB"]["offsuit_broadway"] = 0.72f;
        eqr_profile["BB"]["offsuit"] = 0.55f;
        // SB
        eqr_profile["SB"]["pocket_pair"] = 1.00f;
        eqr_profile["SB"]["suited_connector"] = 0.85f;
        eqr_profile["SB"]["suited"] = 0.80f;
        eqr_profile["SB"]["offsuit_broadway"] = 0.78f;
        eqr_profile["SB"]["offsuit"] = 0.60f;
        // IP_DEFAULT
        eqr_profile["IP_DEFAULT"]["pocket_pair"] = 1.05f;
        eqr_profile["IP_DEFAULT"]["suited_connector"] = 1.08f;
        eqr_profile["IP_DEFAULT"]["suited"] = 1.04f;
        eqr_profile["IP_DEFAULT"]["offsuit_broadway"] = 0.98f;
        eqr_profile["IP_DEFAULT"]["offsuit"] = 0.88f;
        // OOP_DEFAULT
        eqr_profile["OOP_DEFAULT"]["pocket_pair"] = 0.98f;
        eqr_profile["OOP_DEFAULT"]["suited_connector"] = 0.88f;
        eqr_profile["OOP_DEFAULT"]["suited"] = 0.85f;
        eqr_profile["OOP_DEFAULT"]["offsuit_broadway"] = 0.82f;
        eqr_profile["OOP_DEFAULT"]["offsuit"] = 0.70f;
    } else if (profile_type == 2) { // Loose/Aggressive
        // BTN
        eqr_profile["BTN"]["pocket_pair"] = 1.02f;
        eqr_profile["BTN"]["suited_connector"] = 1.15f;
        eqr_profile["BTN"]["suited"] = 1.08f;
        eqr_profile["BTN"]["offsuit_broadway"] = 1.04f;
        eqr_profile["BTN"]["offsuit"] = 1.00f;
        // BB
        eqr_profile["BB"]["pocket_pair"] = 0.88f;
        eqr_profile["BB"]["suited_connector"] = 0.90f;
        eqr_profile["BB"]["suited"] = 0.88f;
        eqr_profile["BB"]["offsuit_broadway"] = 0.85f;
        eqr_profile["BB"]["offsuit"] = 0.78f;
        // SB
        eqr_profile["SB"]["pocket_pair"] = 0.92f;
        eqr_profile["SB"]["suited_connector"] = 0.94f;
        eqr_profile["SB"]["suited"] = 0.92f;
        eqr_profile["SB"]["offsuit_broadway"] = 0.88f;
        eqr_profile["SB"]["offsuit"] = 0.82f;
        // IP_DEFAULT
        eqr_profile["IP_DEFAULT"]["pocket_pair"] = 1.00f;
        eqr_profile["IP_DEFAULT"]["suited_connector"] = 1.10f;
        eqr_profile["IP_DEFAULT"]["suited"] = 1.05f;
        eqr_profile["IP_DEFAULT"]["offsuit_broadway"] = 1.02f;
        eqr_profile["IP_DEFAULT"]["offsuit"] = 0.98f;
        // OOP_DEFAULT
        eqr_profile["OOP_DEFAULT"]["pocket_pair"] = 0.95f;
        eqr_profile["OOP_DEFAULT"]["suited_connector"] = 0.98f;
        eqr_profile["OOP_DEFAULT"]["suited"] = 0.95f;
        eqr_profile["OOP_DEFAULT"]["offsuit_broadway"] = 0.92f;
        eqr_profile["OOP_DEFAULT"]["offsuit"] = 0.88f;
    } else { // Balanced (0)
        load_default_eqr_profile();
    }
}

float PreflopEquityManager::get_eqr_factor(const string& position, const PrivateCards& hand) const {
    string cat = categorize_hand(hand);
    auto pos_it = eqr_profile.find(position);
    if (pos_it != eqr_profile.end()) {
        auto cat_it = pos_it->second.find(cat);
        if (cat_it != pos_it->second.end()) {
            return cat_it->second;
        }
    }
    // Fallback
    string fallback_pos = (position == "BTN" || position == "IP_DEFAULT") ? "IP_DEFAULT" : "OOP_DEFAULT";
    auto fb_it = eqr_profile.find(fallback_pos);
    if (fb_it != eqr_profile.end()) {
        auto cat_it = fb_it->second.find(cat);
        if (cat_it != fb_it->second.end()) {
            return cat_it->second;
        }
    }
    return 1.0f;
}

vector<float> PreflopEquityManager::get_realized_equities(
    const vector<vector<PrivateCards>>& player_ranges,
    const vector<int>& active_players,
    const vector<int>& board
) {
    int total_players = player_ranges.size();
    vector<float> realized_equities(total_players, 0.0f);
    
    int num_active = active_players.size();
    if (num_active == 0) return realized_equities;
    if (num_active == 1) {
        realized_equities[active_players[0]] = 1.0f;
        return realized_equities;
    }

    // Monte Carlo simulation to calculate raw equities of active players
    vector<float> wins(total_players, 0.0f);
    int trials = 1000;
    
    std::random_device rd;
    std::mt19937 mt(rd());

    for (int t = 0; t < trials; ++t) {
        vector<int> deal_board(board);
        // Complete the board if needed
        vector<bool> deck_cards(52, true);
        for (int b : board) deck_cards[b] = false;

        // Deal random hands to active players
        vector<vector<int>> active_hands(total_players);
        bool collision = false;
        
        for (int p_idx : active_players) {
            const auto& range = player_ranges[p_idx];
            if (range.empty()) continue;
            
            // Draw a random hand weighted by weights
            // For simple drafting, choose uniformly or select based on weight
            int rand_idx = mt() % range.size();
            const auto& hand = range[rand_idx];
            
            if (!deck_cards[hand.card1] || !deck_cards[hand.card2]) {
                collision = true;
                break;
            }
            deck_cards[hand.card1] = false;
            deck_cards[hand.card2] = false;
            active_hands[p_idx] = {hand.card1, hand.card2};
        }
        
        if (collision) {
            t--; // retry
            continue;
        }

        // Deal remaining public cards to reach 5 public cards
        while (deal_board.size() < 5) {
            int card = mt() % 52;
            if (deck_cards[card]) {
                deck_cards[card] = false;
                deal_board.push_back(card);
            }
        }

        // Evaluate and compare
        int best_rank = numeric_limits<int>::max();
        vector<int> winners;
        
        for (int p_idx : active_players) {
            int rank = hand_evaluator->get_rank(active_hands[p_idx], deal_board);
            if (rank < best_rank) {
                best_rank = rank;
                winners = {p_idx};
            } else if (rank == best_rank) {
                winners.push_back(p_idx);
            }
        }

        // Distribute wins
        for (int winner : winners) {
            wins[winner] += 1.0f / winners.size();
        }
    }

    // Apply EQR adjustments based on hand categories
    float total_realized = 0.0f;
    for (int p_idx : active_players) {
        float raw_eq = wins[p_idx] / trials;
        
        // Find position name
        string pos = "OOP_DEFAULT";
        if (p_idx == total_players - 1) pos = "BB";
        else if (p_idx == total_players - 2 && total_players >= 2) pos = "SB";
        else if (p_idx == total_players - 3 && total_players >= 3) pos = "BTN";
        
        // Accumulate weighted EQR based on ranges
        float sum_eqr = 0.0f;
        float sum_weight = 0.0f;
        for (const auto& hand : player_ranges[p_idx]) {
            string cat = categorize_hand(hand);
            float eqr_val = eqr_profile[pos][cat];
            if (eqr_val == 0.0f) {
                eqr_val = eqr_profile["OOP_DEFAULT"][cat];
            }
            sum_eqr += eqr_val * hand.weight;
            sum_weight += hand.weight;
        }
        
        float avg_eqr = (sum_weight > 0.0f) ? (sum_eqr / sum_weight) : 1.0f;
        realized_equities[p_idx] = raw_eq * avg_eqr;
        total_realized += realized_equities[p_idx];
    }

    // Normalize equities to sum to 1.0
    if (total_realized > 0.0f) {
        for (int p_idx : active_players) {
            realized_equities[p_idx] /= total_realized;
        }
    }

    return realized_equities;
}
