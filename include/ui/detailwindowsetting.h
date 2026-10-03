#ifndef DETAILWINDOWSETTING_H
#define DETAILWINDOWSETTING_H

#include <QString>
#include <cstring>

class DetailWindowSetting{
public:
    enum DetailWindowMode{
        RANGE_OOP,
        RANGE_IP,
        EV,
        EV_ONLY,
        STRATEGY
    };
    DetailWindowMode mode;
    int grid_i = -1;
    int grid_j = -1;
    int targetPlayer = -1; // -1: use current_player, 0: P1 (IP), 1: P2 (OOP)

    // Expression category selector highlights
    QString selectedCategory;
    bool hasExpressionHighlight = false;
    bool cellHighlight[13][13];
    bool comboHighlight[16];

    DetailWindowSetting();
};

#endif // DETAILWINDOWSETTING_H
