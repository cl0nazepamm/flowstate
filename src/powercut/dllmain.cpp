#include "PowerCutMod.h"

// ── Singleton class descriptor (Meyer's singleton — safe init order) ─
PowerCutClassDesc* GetPowerCutDesc() {
    static PowerCutClassDesc desc;
    return &desc;
}

ClassDesc* GetPowerCutClassDesc() {
    return GetPowerCutDesc();
}
