#include "include/tools/PrivateRangeConverter.h"
#include <unordered_set>
#include <algorithm>

static int getRankVal(char r) {
    if (r == 'A') return 14;
    if (r == 'K') return 13;
    if (r == 'Q') return 12;
    if (r == 'J') return 11;
    if (r == 'T') return 10;
    if (r >= '2' && r <= '9') return r - '0';
    return 0;
}

static char getRankChar(int v) {
    if (v == 14) return 'A';
    if (v == 13) return 'K';
    if (v == 12) return 'Q';
    if (v == 11) return 'J';
    if (v == 10) return 'T';
    if (v >= 2 && v <= 9) return '0' + v;
    return '\0';
}

std::string expandPokerRange(const std::string& range_str) {
    std::string ranks = "AKQJT98765432";
    std::vector<std::string> parts;
    std::string current_part = "";
    
    // Split by comma skipping all whitespaces (spaces, newlines, carriage returns, tabs)
    for (char c : range_str) {
        if (c == ',') {
            if (!current_part.empty()) {
                parts.push_back(current_part);
                current_part = "";
            }
        } else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            current_part += c;
        }
    }
    if (!current_part.empty()) parts.push_back(current_part);
    
    std::vector<std::string> expanded;
    
    for (std::string p : parts) {
        std::string weightStr = "";
        size_t colonIdx = p.find(':');
        if (colonIdx != std::string::npos) {
            weightStr = p.substr(colonIdx);
            p = p.substr(0, colonIdx);
        }
        
        size_t dashIdx = p.find('-');
        if (dashIdx != std::string::npos) {
            std::string start = p.substr(0, dashIdx);
            std::string end = p.substr(dashIdx + 1);
            
            bool processed = false;
            if (start.length() == 2 && end.length() == 2 && start[0] == start[1] && end[0] == end[1]) {
                // Pocket pairs: e.g. 88-QQ
                int v1 = getRankVal(start[0]);
                int v2 = getRankVal(end[0]);
                if (v1 > 0 && v2 > 0) {
                    int min_v = std::min(v1, v2);
                    int max_v = std::max(v1, v2);
                    for (char c : ranks) {
                        int v = getRankVal(c);
                        if (v >= min_v && v <= max_v) {
                            expanded.push_back(std::string(2, c) + weightStr);
                        }
                    }
                    processed = true;
                }
            } else if (start.length() == 3 && end.length() == 3 && start[2] == end[2]) {
                char suffix = start[2];
                char h1 = start[0], l1 = start[1];
                char h2 = end[0], l2 = end[1];
                int vh1 = getRankVal(h1), vl1 = getRankVal(l1);
                int vh2 = getRankVal(h2), vl2 = getRankVal(l2);
                
                if (vh1 > 0 && vl1 > 0 && vh2 > 0 && vl2 > 0) {
                    if (h1 == h2) {
                        // Same high card: e.g. A2s-A5s
                        int min_v = std::min(vl1, vl2);
                        int max_v = std::max(vl1, vl2);
                        for (char c : ranks) {
                            int v = getRankVal(c);
                            if (v >= min_v && v <= max_v && v < vh1) {
                                expanded.push_back(std::string(1, h1) + c + suffix + weightStr);
                            }
                        }
                        processed = true;
                    } else {
                        // Same gap: e.g. T9s-KQs
                        int gap1 = vh1 - vl1;
                        int gap2 = vh2 - vl2;
                        if (gap1 == gap2) {
                            int min_vh = std::min(vh1, vh2);
                            int max_vh = std::max(vh1, vh2);
                            for (char c : ranks) {
                                int vh = getRankVal(c);
                                if (vh >= min_vh && vh <= max_vh) {
                                    int vl = vh - gap1;
                                    char lc = getRankChar(vl);
                                    if (lc != '\0') {
                                        expanded.push_back(std::string(1, c) + lc + suffix + weightStr);
                                    }
                                }
                            }
                            processed = true;
                        }
                    }
                }
            }
            if (!processed) {
                expanded.push_back(p + weightStr);
            }
        } else if (p.find('+') != std::string::npos) {
            std::string base = p.substr(0, p.length() - 1);
            if (base.length() == 2 && base[0] == base[1]) {
                size_t start_idx = ranks.find(base[0]);
                for (int i = start_idx; i >= 0; --i) {
                    expanded.push_back(std::string(1, ranks[i]) + ranks[i] + weightStr);
                }
            } else if (base.length() == 3) {
                char high = base[0];
                char low = base[1];
                char suit = base[2];
                size_t high_idx = ranks.find(high);
                size_t low_idx = ranks.find(low);
                if (high_idx != std::string::npos && low_idx != std::string::npos) {
                    if (low_idx - high_idx == 1) {
                        for (int i = high_idx; i >= 0; --i) {
                            char curr_high = ranks[i];
                            char curr_low = ranks[i+1];
                            expanded.push_back(std::string(1, curr_high) + curr_low + suit + weightStr);
                        }
                    } else {
                        for (size_t i = low_idx; i >= high_idx && i != std::string::npos; --i) {
                            if (ranks[i] == high) continue;
                            expanded.push_back(std::string(1, high) + ranks[i] + suit + weightStr);
                        }
                    }
                }
            } else {
                expanded.push_back(p + weightStr);
            }
        } else {
            expanded.push_back(p + weightStr);
        }
    }
    
    std::string result = "";
    std::unordered_set<std::string> seen;
    bool first = true;
    for (size_t i = 0; i < expanded.size(); ++i) {
        if (seen.find(expanded[i]) == seen.end()) {
            seen.insert(expanded[i]);
            if (!first) result += ",";
            result += expanded[i];
            first = false;
        }
    }
    return result;
}

vector<PrivateCards> PrivateRangeConverter::rangeStr2Cards(string range_str, vector<int> initial_boards) {
    std::cout << "DEBUG: rangeStr2Cards called with range_str: " << range_str << std::endl;
    range_str = expandPokerRange(range_str);
    vector<string> range_list = string_split(range_str,',');
    vector<PrivateCards> private_cards;

    uint64_t initial_board_long = 0;
    if (!initial_boards.empty()) {
        initial_board_long = Card::boardInts2long(initial_boards);
    }

    for(string one_range:range_list){
        PrivateCards this_card;
        vector<string> cardstr_arr = string_split(one_range,':');
        if (cardstr_arr.empty()){
            throw runtime_error("range item is empty");
        }
        float weight = 1;

        one_range = cardstr_arr[0];
        if(cardstr_arr.size() >= 2){
            weight = atof(cardstr_arr[1].c_str());
        }
        if(weight <= 0.005){
            continue;
        }

        int range_len = one_range.length();
        // TODO finish here , convert str to a PrivateRanges[]
        if(range_len == 3){
            if(one_range.at(2) == 's'){
                char rank1 = one_range.at(0);
                char rank2 = one_range.at(1);
                if(rank1 == rank2) throw runtime_error(tfm::format("%s%ss is not a valid card desc",rank1,rank2));
                for(const string& one_suit :Card::getSuits()){
                    int card1 = Card::strCard2int(rank1 + one_suit);
                    int card2 = Card::strCard2int(rank2 + one_suit);
                    if(Card::boardsHasIntercept(
                            Card::boardInts2long(vector<int>{card1,card2}),
                            initial_board_long
                    )){
                        continue;
                    }
                    this_card = PrivateCards(card1,card2,weight);
                    private_cards.push_back(this_card);
                }

            }else if(one_range.at(2) == 'o'){
                char rank1 = one_range.at(0);
                char rank2 = one_range.at(1);

                vector<string> suits = Card::getSuits();
                for(std::size_t i = 0;i < suits.size();i++){
                    string one_suit = suits[i];
                    int begin_index = rank1 == rank2 ? i:0;
                    for(std::size_t j = begin_index;j < suits.size();j++){
                        string another_suit = suits[j];
                        if(one_suit == another_suit){
                            continue;
                        }
                        int card1 = Card::strCard2int(rank1 + one_suit);
                        int card2 = Card::strCard2int(rank2 + another_suit);
                        if(Card::boardsHasIntercept(
                                Card::boardInts2long(vector<int>{card1,card2}),
                                initial_board_long
                        )){
                            continue;
                        }
                        this_card = PrivateCards(card1, card2, weight);
                        private_cards.push_back(this_card);
                    }
                }
            }else{
                throw runtime_error("format not recognize");
            }
        }else if(range_len == 2){
            char rank1 = one_range.at(0);
            char rank2 = one_range.at(1);
            vector<string> suits = Card::getSuits();
            for(std::size_t i = 0;i < suits.size();i++){
                string one_suit = suits[i];
                int begin_index = rank1 == rank2 ? i:0;
                for(std::size_t j = begin_index;j < suits.size();j++){
                    string another_suit = suits[j];
                    if(one_suit == another_suit && rank1 == rank2){
                        continue;
                    }
                    int card1 = Card::strCard2int(rank1 + one_suit);
                    int card2 = Card::strCard2int(rank2 + another_suit);
                    if(Card::boardsHasIntercept(
                            Card::boardInts2long(vector<int>{card1,card2}),
                            initial_board_long
                    )){
                        continue;
                    }
                    this_card = PrivateCards(card1, card2, weight);
                    private_cards.push_back(this_card);
                }
            }

        }else throw runtime_error(tfm::format(" range str %s len not valid ",one_range));
    }


    // 排除初试range中重复的情况
    for(std::size_t i = 0;i < private_cards.size();i ++){
        for(std::size_t j = i + 1;j < private_cards.size();j ++) {
            PrivateCards one_cards = private_cards[i];
            PrivateCards another_cards = private_cards[j];
            if (one_cards.card1 == another_cards.card1 && one_cards.card2 == another_cards.card2){
                throw runtime_error(tfm::format("card %s %s duplicate"
                        , Card::intCard2Str(one_cards.card1)
                        , Card::intCard2Str(one_cards.card2)
                ));
            }
            if(one_cards.card1 == another_cards.card2 && one_cards.card2 == another_cards.card1) {
                throw runtime_error(tfm::format("card %s %s duplicate"
                        , Card::intCard2Str(one_cards.card1)
                        , Card::intCard2Str(one_cards.card2)
                ));
            }
        }
    }

    vector<PrivateCards> private_cards_list(private_cards.size());
    for(std::size_t i = 0;i < private_cards.size();i ++){
        private_cards_list[i] = private_cards[i];
        //System.out.print(String.format("[%s-%s]",Card.intCard2Str(private_cards_list[i].card1),Card.intCard2Str(private_cards_list[i].card2)));
    }
    /*
        output all private combos

    System.out.println("private range number:");
    System.out.println(private_cards.size());
    System.out.println();
     */
    return private_cards_list;

}
