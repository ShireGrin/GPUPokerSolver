#ifndef TEXASSOLVER_PREFLOPEQUITYMANAGER_H
#define TEXASSOLVER_PREFLOPEQUITYMANAGER_H

#include <vector>
#include <string>
#include <map>
#include "include/ranges/PrivateCards.h"
#include "include/compairer/Compairer.h"

using namespace std;

class PreflopEquityManager {
private:
    shared_ptr<Compairer> hand_evaluator;
    
    // Position-based EQR map: EQR[position_name][hand_category_string] = factor
    // E.g. EQR["BB"]["suited_connector"] = 0.85f
    map<string, map<string, float>> eqr_profile;
    
    // Fallback EQR profiles
    void load_default_eqr_profile();

    // Utility to categorize a starting hand (e.g., "pocket_pair", "suited_connector", "offsuit_broadway")
    string categorize_hand(const PrivateCards& hand) const;

public:
    PreflopEquityManager(shared_ptr<Compairer> evaluator);

    // Set the EQR profile type (0: Balanced, 1: Tight, 2: Loose/Aggressive)
    void set_profile_type(int profile_type);

    // Get EQR factor for a position and hand
    float get_eqr_factor(const string& position, const PrivateCards& hand) const;

    // Load custom EQR profiles from JSON config file
    void load_eqr_profile(const string& json_path);

    // Get the realized equity of each active player given their ranges and the board
    vector<float> get_realized_equities(
        const vector<vector<PrivateCards>>& player_ranges,
        const vector<int>& active_players,
        const vector<int>& board
    );
};

#endif // TEXASSOLVER_PREFLOPEQUITYMANAGER_H
