#include "include/ui/detailwindowsetting.h"

DetailWindowSetting::DetailWindowSetting(){
    this->mode = DetailWindowMode::STRATEGY;
    this->hasExpressionHighlight = false;
    this->targetPlayer = -1;
    memset(cellHighlight, 0, sizeof(cellHighlight));
    memset(comboHighlight, 0, sizeof(comboHighlight));
}
