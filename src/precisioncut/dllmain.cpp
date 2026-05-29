#include "PrecisionCutMod.h"

// ── Singleton class descriptor (Meyer's singleton — safe init order) ─
PrecisionCutClassDesc* GetPrecisionCutDesc() {
    static PrecisionCutClassDesc desc;
    return &desc;
}

ClassDesc* GetPrecisionCutClassDesc() {
    return GetPrecisionCutDesc();
}
