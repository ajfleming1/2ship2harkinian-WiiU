#include <libultraship/bridge.h>
#include "Enhancements/GameInteractor/GameInteractor.h"

extern "C" {
#include "z64.h"
#include "functions.h"
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
}

void RegisterSkipEntranceCutscenes() {
    REGISTER_VB_SHOULD(GI_VB_PLAY_ENTRANCE_CS, {
        if (CVarGetInteger("gEnhancements.Cutscenes.SkipEntranceCutscenes", 0)) {
            if (gPlayState != NULL && gPlayState->sceneId == SCENE_CLOCKTOWER && !gSaveContext.save.isFirstCycle) {
                Sram_SaveSpecialEnterClockTown(gPlayState);
            }
            *should = false;
        }
    });
}
