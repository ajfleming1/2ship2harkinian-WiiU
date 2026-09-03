#include "2s2h/resource/importer/scenecommand/SetCutscenesFactory.h"
#include "2s2h/resource/type/scenecommand/SetCutscenes.h"
#include <libultraship/libultraship.h>
#include "spdlog/spdlog.h"

namespace SOH {
std::shared_ptr<Ship::IResource> SetCutsceneFactoryMM::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                                                    std::shared_ptr<Ship::BinaryReader> reader) {
    auto setCutscenes = std::make_shared<SetCutscenesMM>(initData);

    ReadCommandId(setCutscenes, reader);

    size_t numCs = reader->ReadUByte();
    setCutscenes->entries.reserve(numCs);

    for (size_t i = 0; i < numCs; i++) {
        CutsceneScriptEntry entry;
        std::string path = reader->ReadString();
        entry.exit = reader->ReadUInt16();
        entry.entrance = reader->ReadUByte();
        entry.flag = reader->ReadUByte();
        auto resource = Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(path.c_str());
        if (resource != nullptr) {
            entry.data = std::static_pointer_cast<Cutscene>(resource)->GetPointer();
        } else {
            // Provide a valid empty cutscene script so the game doesn't crash in Cutscene_ProcessScript
            // 1 total entry, 0 csFrameCount, CS_CAM_STOP command (-1)
            static uint32_t sEmptyCutscene[] = { 1, 0, 0xFFFFFFFF };
            entry.data = sEmptyCutscene;
            SPDLOG_ERROR("Failed to load cutscene: {}", path);
        }
        setCutscenes->entries.emplace_back(entry);
    }

    return setCutscenes;
}
} // namespace SOH
