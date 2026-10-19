#include "SaveManager.h"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <memory>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>

#include "macros.h"
#include "BenJsonConversions.hpp"
#include "BenPort.h"

extern "C" {
#include "src/overlays/gamestates/ovl_file_choose/z_file_select.h"
extern FileSelectState* gFileSelectState;
}

// This entire thing is temporary until we have a more robust save system that
// supports backwards compatability, migrations, threaded saving, save sections, etc.
typedef enum FlashSlotFile {
    /* -1 */ FLASH_SLOT_FILE_UNAVAILABLE = -1,
    /*  0 */ FLASH_SLOT_FILE_1_NEW_CYCLE,
    /*  1 */ FLASH_SLOT_FILE_1_NEW_CYCLE_BACKUP,
    /*  2 */ FLASH_SLOT_FILE_2_NEW_CYCLE,
    /*  3 */ FLASH_SLOT_FILE_2_NEW_CYCLE_BACKUP,
    /*  4 */ FLASH_SLOT_FILE_1_OWL_SAVE,
    /*  5 */ FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP,
    /*  6 */ FLASH_SLOT_FILE_2_OWL_SAVE,
    /*  7 */ FLASH_SLOT_FILE_2_OWL_SAVE_BACKUP,
    /*  8 */ FLASH_SLOT_FILE_SRAM_HEADER,
    /*  9 */ FLASH_SLOT_FILE_SRAM_HEADER_BACKUP,
} FlashSlotFile;

#ifdef __WIIU__
#include <whb/log.h>
#include <port/wiiu/WiiUImpl.h>
#endif

#define GET_NEWF(save, index) (save.saveInfo.playerData.newf[index])
#define IS_VALID_FILE(save)                                                                    \
    ((GET_NEWF(save, 0) == 'Z') && (GET_NEWF(save, 1) == 'E') && (GET_NEWF(save, 2) == 'L') && \
     (GET_NEWF(save, 3) == 'D') && (GET_NEWF(save, 4) == 'A') && (GET_NEWF(save, 5) == '3'))

static const std::filesystem::path& GetSavesFolderPath() {
    static const std::filesystem::path folder = Ship::Context::GetPathRelativeToAppDirectory("saves", appShortName);
    return folder;
}
#define savesFolderPath GetSavesFolderPath()

// Migrations
// The idea here is that we can read in any version of the save as generic JSON, then apply migrations
// to the JSON to ensure it's in the correct shape for the current to_json/from_json helpers to convert
// it to the current struct that the game uses.
//
// To add a new migration:
// - Increment CURRENT_SAVE_VERSION
// - Create the migration file in the Migrations folder with the name `{CURRENT_SAVE_VERSION}.cpp`
// - Add the migration function definition below and add it to the `migrations` map with the key being the previous
// version
const uint32_t CURRENT_SAVE_VERSION = 5;

void SaveManager_Migration_1(nlohmann::json& j);
void SaveManager_Migration_2(nlohmann::json& j);
void SaveManager_Migration_3(nlohmann::json& j);
void SaveManager_Migration_4(nlohmann::json& j);
void SaveManager_Migration_5(nlohmann::json& j);

const std::unordered_map<uint32_t, std::function<void(nlohmann::json&)>> migrations = {
    // Pre-1.0.0 Migrations, deprecated
    { 0, SaveManager_Migration_1 },
    { 1, SaveManager_Migration_2 },
    { 2, SaveManager_Migration_3 },
    { 3, SaveManager_Migration_4 },
    // Base Migration
    { 4, SaveManager_Migration_5 },
};

int SaveManager_MigrateSave(nlohmann::json& j) {
    int version = j.value("version", 0);

    if (version > CURRENT_SAVE_VERSION) {
        SPDLOG_ERROR("Save version is greater than current version");
        return -1;
    }

    if (version >= 4 && !j.contains("newCycleSave") && !j.contains("owlSave")) {
        SPDLOG_ERROR("Save file is missing newCycleSave and owlSave");
        return -1;
    }

    try {
        while (version < CURRENT_SAVE_VERSION) {
            if (migrations.contains(version)) {
                auto migration = migrations.at(version);
                if (version < 4) {
                    migration(j); // Pre-1.0.0 Migrations, deprecated
                } else {
                    // In the case of copying files, the owl save is copied first, so the new cycle may not exist yet.
                    if (j.contains("newCycleSave")) {
                        migration(j["newCycleSave"]);
                    }
                    // Only migrate the owl save if it exists
                    if (j.contains("owlSave")) {
                        migration(j["owlSave"]);
                    }
                }
            }
            version = j["version"] = version + 1;
        }
        return 0;
    } catch (...) {
        SPDLOG_ERROR("Failed to migrate save file");
        return -1;
    }
}

static void SaveManager_Notify(const std::string& message) {
#ifdef __WIIU__
    WHBLogPrintf("2ship: [save] %s", message.c_str());
    try {
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(6.0f, true, "%s", message.c_str());
    } catch (...) {}
#else
    (void)message;
#endif
}

// Reads the whole file into memory. Returns 0 on success, -1 if missing, -3 on I/O error.
static int SaveManager_ReadFileToString(const std::filesystem::path& filePath, std::string& out) {
    FILE* fp = fopen(filePath.string().c_str(), "rb");
    if (fp == nullptr) {
        return -1;
    }
    out.clear();
    char buf[16 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        out.append(buf, n);
    }
    bool err = ferror(fp) != 0;
    fclose(fp);
    return err ? -3 : 0;
}

static bool SaveManager_WriteSaveFileImpl(const std::filesystem::path& fileName, const nlohmann::json& j) {
    const std::filesystem::path filePath = savesFolderPath / fileName;

    try {
        if (!std::filesystem::exists(savesFolderPath)) {
            std::filesystem::create_directory(savesFolderPath);
        }

#ifdef __WIIU__
        // Compact JSON: pretty-printing makes the save ~4x larger (the pictograph alone is 11k array entries),
        // which means much slower SD writes and much higher peak memory while parsing on load.
        const std::string data = j.dump() + "\n";
        WHBLogPrintf("2ship: SaveManager_WriteSaveFile: %s (%zu bytes)", filePath.string().c_str(), data.size());
#else
        const std::string data = j.dump(4) + "\n";
#endif

        FILE* fp = fopen(filePath.string().c_str(), "wb");
        if (fp == nullptr) {
            SPDLOG_ERROR("Failed to open save file for writing: {}", filePath.string());
            if (errno == 30) {
                SaveManager_Notify("Save FAILED: SD card is WRITE-PROTECTED (errno 30)!\nCheck SD lock switch or adapter");
            } else {
                SaveManager_Notify("Save FAILED: could not open " + filePath.filename().string() +
                                   " (errno " + std::to_string(errno) + ")");
            }
            return false;
        }
        size_t written = fwrite(data.data(), 1, data.size(), fp);
        bool ok = (written == data.size()) && (fflush(fp) == 0);
#ifndef _WIN32
        fsync(fileno(fp));
#endif
        ok = (fclose(fp) == 0) && ok;
        if (!ok) {
            SPDLOG_ERROR("Failed to write save file: {}", filePath.string());
            SaveManager_Notify("Save FAILED: wrote " + std::to_string(written) + "/" + std::to_string(data.size()) +
                               " bytes to " + fileName.string() + " (SD card full or write-protected?)");
            return false;
        }

#ifdef __WIIU__
        Ship::WiiU::FlushVolume();
        WHBLogPrintf("2ship: SaveManager_WriteSaveFile completed for %s", filePath.string().c_str());
#endif
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to write save file: {}", e.what());
        SaveManager_Notify(std::string("Save FAILED: ") + e.what());
    } catch (...) {
        SPDLOG_ERROR("Failed to write save file");
        SaveManager_Notify("Save FAILED: unknown error writing " + fileName.string());
    }
    return false;
}

void SaveManager_WriteSaveFile(std::filesystem::path fileName, nlohmann::json j) {
    SaveManager_WriteSaveFileImpl(fileName, j);
}

void SaveManager_DeleteSaveFile(std::filesystem::path fileName) {
    const std::filesystem::path filePath = savesFolderPath / fileName;

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::remove(filePath);
#ifdef __WIIU__
            Ship::WiiU::FlushVolume();
            WHBLogPrintf("2ship: SaveManager_DeleteSaveFile: deleted %s and flushed volume", filePath.string().c_str());
#endif
        }
    } catch (...) { SPDLOG_ERROR("Failed to delete save file"); }
}

// Returns 0 on success, -1 if the file doesn't exist, -2 if the file is corrupt (bad JSON),
// -3 on a transient failure (out of memory / I/O error) where the file itself may be perfectly fine.
int SaveManager_ReadSaveFile(std::filesystem::path fileName, nlohmann::json& j) {
    const std::filesystem::path filePath = savesFolderPath / fileName;

    if (!std::filesystem::exists(filePath)) {
#ifdef __WIIU__
        WHBLogPrintf("2ship: SaveManager_ReadSaveFile: file not found: %s", filePath.string().c_str());
#endif
        return -1;
    }

    try {
        std::string data;
        int ioResult = SaveManager_ReadFileToString(filePath, data);
        if (ioResult != 0) {
#ifdef __WIIU__
            WHBLogPrintf("2ship: SaveManager_ReadSaveFile: I/O error %d reading %s", ioResult,
                         filePath.string().c_str());
#endif
            return ioResult == -1 ? -1 : -3;
        }
        j = nlohmann::json::parse(data);
#ifdef __WIIU__
        WHBLogPrintf("2ship: SaveManager_ReadSaveFile: successfully loaded %s (%zu bytes)", filePath.string().c_str(),
                     data.size());
#endif
        return 0;
    } catch (const nlohmann::json::parse_error& e) {
#ifdef __WIIU__
        WHBLogPrintf("2ship: SaveManager_ReadSaveFile: FAILED to parse %s: %s", filePath.string().c_str(), e.what());
#endif
        SPDLOG_ERROR("Failed to parse save file: {}", e.what());
        return -2;
    } catch (const std::bad_alloc&) {
        // Never treat running out of memory as a corrupt save: the caller would move the (good) file aside and
        // fall back to the stale backup.
        SaveManager_Notify("Out of memory while reading " + fileName.string());
        return -3;
    } catch (...) {
        SaveManager_Notify("Unknown error while reading " + fileName.string());
        return -3;
    }
}

void SaveManager_MoveInvalidSaveFile(std::filesystem::path fileName, std::string message) {
    const std::filesystem::path filePath = savesFolderPath / fileName;
    const std::filesystem::path backupFilePath =
        savesFolderPath / (fileName.stem().string() + "_invalid_" + std::to_string(std::time(nullptr)) + ".json");

    try {
        if (std::filesystem::exists(filePath)) {
            std::filesystem::rename(filePath, backupFilePath);
        }

        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, message.c_str());
    } catch (...) { SPDLOG_ERROR("Failed to move invalid save file"); }
}

int SaveManager_GetOpenFileSlot() {
    std::string fileName = "file1.json";
    if (!std::filesystem::exists(savesFolderPath / fileName)) {
        return 1;
    }

    fileName = "file2.json";
    if (!std::filesystem::exists(savesFolderPath / fileName)) {
        return 2;
    }

    return -1;
}

FlashSlotFile SaveManager_GetFlashSlotFileFromPages(u32 pageNum, u32 pageCount) {
    FlashSlotFile flashSlotFile = FLASH_SLOT_FILE_UNAVAILABLE;

    for (u32 i = 0; i < ARRAY_COUNT(gFlashSaveStartPages) - 1; i++) {
        // Verify that the requested pages align with expected values
        if (pageNum == (u32)gFlashSaveStartPages[i] &&
            (pageCount == (u32)gFlashSaveNumPages[i] || pageCount == (u32)gFlashSpecialSaveNumPages[i])) {
            flashSlotFile = static_cast<FlashSlotFile>(i);
            break;
        }
    }

    return flashSlotFile;
}

std::string SaveManager_GetFileName(int fileNum, bool isBackup) {
    return "file" + std::to_string(fileNum) + (isBackup ? "backup" : "") + ".json";
}

std::string SaveManager_GetFileNameFromFlashSlotFile(FlashSlotFile flashSlotFile) {
    if (flashSlotFile == FLASH_SLOT_FILE_UNAVAILABLE)
        return "invalid";

    if (flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER || flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER_BACKUP) {
        return "global.json";
    }

    bool isBackup =
        flashSlotFile == FLASH_SLOT_FILE_1_NEW_CYCLE_BACKUP || flashSlotFile == FLASH_SLOT_FILE_2_NEW_CYCLE_BACKUP ||
        flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP || flashSlotFile == FLASH_SLOT_FILE_2_OWL_SAVE_BACKUP;

    int fileNum =
        (flashSlotFile == FLASH_SLOT_FILE_1_NEW_CYCLE_BACKUP || flashSlotFile == FLASH_SLOT_FILE_1_NEW_CYCLE ||
         flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP || flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE)
            ? 1
            : 2;

    bool isOwlSave =
        (flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE || flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP ||
         flashSlotFile == FLASH_SLOT_FILE_2_OWL_SAVE || flashSlotFile == FLASH_SLOT_FILE_2_OWL_SAVE_BACKUP);

    return "file" + std::to_string(fileNum) + (isBackup ? "backup" : "") + ".json";
}

bool SaveManager_HandleFileDropped(std::string filePath) {
    try {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            return false;
        }

        // Check if first byte is "{"
        if (fileStream.peek() != '{') {
            return false;
        }

        nlohmann::json j;
        try {
            fileStream >> j;
        } catch (nlohmann::json::parse_error& e) { return false; }

        if (!j.contains("type") || j["type"] != "2S2H_SAVE") {
            return false;
        }

        int saveSlot = SaveManager_GetOpenFileSlot();
        if (saveSlot == -1) {
            SPDLOG_ERROR("No save slot available");
            auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
            gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "No save slot available");
            return true;
        }

        std::string fileName = SaveManager_GetFileName(saveSlot);

        SaveManager_WriteSaveFile(fileName, j);

        if (gFileSelectState != NULL) {
            func_801457CC(&gFileSelectState->state, &gFileSelectState->sramCtx);
            if (gFileSelectState->menuMode == FS_MENU_MODE_CONFIG && gFileSelectState->configMode == CM_MAIN_MENU) {
                gFileSelectState->configMode = CM_FADE_IN_START;
            }
        }

        return true;
    } catch (std::exception& e) {
        SPDLOG_ERROR("Failed to load file: {}", e.what());
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load file");
        return false;
    } catch (...) {
        SPDLOG_ERROR("Failed to load file");
        auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
        gui->GetGameOverlay()->TextDrawNotification(30.0f, true, "Failed to load file");
        return false;
    }
}

extern "C" void SaveManager_SysFlashrom_WriteData(u8* saveBuffer, u32 pageNum, u32 pageCount) {
    FlashSlotFile flashSlotFile = SaveManager_GetFlashSlotFileFromPages(pageNum, pageCount);
    std::string fileName = SaveManager_GetFileNameFromFlashSlotFile(flashSlotFile);

    bool isBackup = false;

    if (flashSlotFile == FLASH_SLOT_FILE_UNAVAILABLE) {
        return;
    }

    if (flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER || flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER_BACKUP) {
        SaveOptions saveOptions;
        memcpy(&saveOptions, saveBuffer, sizeof(SaveOptions));

        SaveManager_WriteSaveFile(fileName, saveOptions);
        return;
    }

    switch (flashSlotFile) {
        case FLASH_SLOT_FILE_1_NEW_CYCLE_BACKUP:
        case FLASH_SLOT_FILE_2_NEW_CYCLE_BACKUP:
            isBackup = true;
            // fallthrough
        case FLASH_SLOT_FILE_1_NEW_CYCLE:
        case FLASH_SLOT_FILE_2_NEW_CYCLE: {
            Save save;
            memcpy(&save, saveBuffer, sizeof(Save));

            if (IS_VALID_FILE(save)) {
                nlohmann::json j;

                // Read the existing save file to preserve the owl save
                int result = SaveManager_ReadSaveFile(fileName, j);
                if (result == -3) {
                    // Writing now would drop the owl save that we failed to read
                    SaveManager_Notify("Save FAILED: could not read existing " + fileName);
                    break;
                } else if (result == -2) {
                    SaveManager_MoveInvalidSaveFile(
                        fileName,
                        "Something went wrong trying to preserve the owl save. Original save file has been backed up.");
                } else if (result == 0) {
                    result = SaveManager_MigrateSave(j);

                    if (result != 0) {
                        SaveManager_MoveInvalidSaveFile(
                            fileName, "Failed to migrate owl save. Original save file has been backed up.");
                    }
                }

                j["newCycleSave"]["save"] = save;
                j["version"] = CURRENT_SAVE_VERSION;
                j["type"] = "2S2H_SAVE";

                SaveManager_WriteSaveFile(fileName, j);

                if (pageCount == (u32)gFlashSpecialSaveNumPages[flashSlotFile]) {
                    std::string backupFileName =
                        SaveManager_GetFileName((flashSlotFile == FLASH_SLOT_FILE_1_NEW_CYCLE) ? 1 : 2, true);
                    Save backupSave;
                    memcpy(&backupSave, saveBuffer + 0x2000, sizeof(Save));
                    if (IS_VALID_FILE(backupSave)) {
                        nlohmann::json bj;
                        int bresult = SaveManager_ReadSaveFile(backupFileName, bj);
                        if (bresult == 0) {
                            SaveManager_MigrateSave(bj);
                        }
                        bj["newCycleSave"]["save"] = backupSave;
                        bj["version"] = CURRENT_SAVE_VERSION;
                        bj["type"] = "2S2H_SAVE";
                        SaveManager_WriteSaveFile(backupFileName, bj);
                    }
                }
            } else {
                // If IS_VALID_FILE fails, we should delete the save file, even if there is an owl save in it, because
                // they just deleted the new cycle save
                SaveManager_DeleteSaveFile(fileName);
            }
            break;
        }
        case FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP:
        case FLASH_SLOT_FILE_2_OWL_SAVE_BACKUP:
            isBackup = true;
            // fallthrough
        case FLASH_SLOT_FILE_1_OWL_SAVE:
        case FLASH_SLOT_FILE_2_OWL_SAVE: {
            // ~18 KB; keep it off the (small) main thread stack
            std::unique_ptr<SaveContext> saveContextPtr = std::make_unique<SaveContext>();
            SaveContext& saveContext = *saveContextPtr;
            memcpy(&saveContext, saveBuffer, offsetof(SaveContext, fileNum));

            nlohmann::json j;
            // Read the existing save file to preserve the new cycle save
            int result = SaveManager_ReadSaveFile(fileName, j);
            if (result == -3) {
                // Writing (or worse, deleting) now would drop the new cycle save that we failed to read
                SaveManager_Notify("Save FAILED: could not read existing " + fileName);
                break;
            } else if (result == -2) {
                SaveManager_MoveInvalidSaveFile(fileName, "Something went wrong trying to preserve the new cycle save. "
                                                          "Original save file has been backed up.");
            } else if (result == 0) {
                result = SaveManager_MigrateSave(j);

                if (result != 0) {
                    SaveManager_MoveInvalidSaveFile(
                        fileName, "Failed to migrate new cycle save. Original save file has been backed up.");
                }
            }

            if (IS_VALID_FILE(saveContext.save)) {
                j["owlSave"] = saveContext;
                j["version"] = CURRENT_SAVE_VERSION;
                j["type"] = "2S2H_SAVE";
                SaveManager_WriteSaveFile(fileName, j);
            } else {
                // If IS_VALID_FILE fails, and there is still a new cycle save present, we just want to only remove the
                // owl save and write the new cycle save back
                if (j.contains("newCycleSave")) {
                    if (j.contains("owlSave")) {
                        j.erase("owlSave");
                    }
                    j["version"] = CURRENT_SAVE_VERSION;
                    j["type"] = "2S2H_SAVE";
                    SaveManager_WriteSaveFile(fileName, j);
                    // If there is no new cycle save, we should just delete the save file
                } else {
                    SaveManager_DeleteSaveFile(fileName);
                }
            }
            break;
        }
    }
}

extern "C" s32 SaveManager_SysFlashrom_ReadData(void* saveBuffer, u32 pageNum, u32 pageCount) {
    FlashSlotFile flashSlotFile = SaveManager_GetFlashSlotFileFromPages(pageNum, pageCount);
    std::string fileName = SaveManager_GetFileNameFromFlashSlotFile(flashSlotFile);

    if (flashSlotFile == FLASH_SLOT_FILE_UNAVAILABLE) {
        return -1;
    }

    if (flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER || flashSlotFile == FLASH_SLOT_FILE_SRAM_HEADER_BACKUP) {
        nlohmann::json j;
        int result = SaveManager_ReadSaveFile(fileName, j);
        if (result == -2) {
            SaveManager_MoveInvalidSaveFile(
                fileName,
                "Something went wrong trying to read global save file, the original file has been backed up.");
            return -1;
        } else if (result != 0) {
            return result;
        }

        SaveOptions saveOptions = j;

        memcpy(saveBuffer, &saveOptions, sizeof(SaveOptions));
        return 0;
    }

    bool isOwlSave =
        (flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE || flashSlotFile == FLASH_SLOT_FILE_1_OWL_SAVE_BACKUP ||
         flashSlotFile == FLASH_SLOT_FILE_2_OWL_SAVE || flashSlotFile == FLASH_SLOT_FILE_2_OWL_SAVE_BACKUP);

    nlohmann::json j;
    int result = SaveManager_ReadSaveFile(fileName, j);
    if (result == -2) {
        SaveManager_MoveInvalidSaveFile(
            fileName, "Something went wrong trying to read save file, the original file has been backed up.");
        return -1;
    } else if (result != 0) {
        return result;
    }

    result = SaveManager_MigrateSave(j);

    if (result != 0) {
        SaveManager_MoveInvalidSaveFile(fileName, "Failed to migrate save file, the original file has been backed up.");
        return -1;
    }

    if (isOwlSave) {
        if (!j.contains("owlSave")) {
            return -1;
        }

        std::unique_ptr<SaveContext> saveContextPtr = std::make_unique<SaveContext>();
        try {
            j.at("owlSave").get_to(*saveContextPtr);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to convert owl save from {}: {}", fileName, e.what());
            SaveManager_Notify("Owl save in " + fileName + " is unreadable: " + e.what());
            return -1;
        }
#ifdef __WIIU__
        WHBLogPrintf("2ship: ReadData %s owlSave: day=%d time=0x%04X rupees=%d isOwlSave=%d pauseEntrance=0x%X",
                     fileName.c_str(), (int)saveContextPtr->save.day, (int)saveContextPtr->save.time,
                     (int)saveContextPtr->save.saveInfo.playerData.rupees, (int)saveContextPtr->save.isOwlSave,
                     (int)saveContextPtr->save.shipSaveInfo.pauseSaveEntrance);
#endif

        memcpy(saveBuffer, saveContextPtr.get(), offsetof(SaveContext, fileNum));
        return 0;
    } else {
        if (!j.contains("newCycleSave")) {
            return -1;
        }

        Save save = j["newCycleSave"]["save"];

        memcpy(saveBuffer, &save, sizeof(Save));
        return 0;
    }
}
