#include "2s2h/resource/importer/scenecommand/SetPathwaysFactory.h"
#include "2s2h/resource/type/scenecommand/SetPathways.h"
#include "spdlog/spdlog.h"
#include <libultraship/libultraship.h>

namespace SOH {
std::shared_ptr<Ship::IResource> SetPathwaysMMFactory::ReadResource(std::shared_ptr<Ship::ResourceInitData> initData,
                                                                    std::shared_ptr<Ship::BinaryReader> reader) {
    auto setPathways = std::make_shared<SetPathwaysMM>(initData);

    ReadCommandId(setPathways, reader);

    setPathways->numPaths = reader->ReadUInt32();
    setPathways->paths.reserve(setPathways->numPaths);
    for (uint32_t i = 0; i < setPathways->numPaths; i++) {
        std::string pathFileName = reader->ReadString();
        auto path = std::static_pointer_cast<PathMM>(
            Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(pathFileName.c_str()));
        if (path != nullptr) {
            setPathways->paths.push_back(path->GetPointer());
        } else {
            setPathways->paths.push_back(nullptr);
            SPDLOG_ERROR("Failed to load pathway: {}", pathFileName);
        }
    }
    return setPathways;
}
} // namespace SOH
