#ifndef KICKCAT_EEPROM_EDITOR_APP_H
#define KICKCAT_EEPROM_EDITOR_APP_H

#include <string>
#include <vector>

#include "imgui.h"
#include "vendor/imgui_memory_editor.h"

#include "kickcat/SIIParser.h"

namespace kickcat::eeprom_editor
{
    enum class Category
    {
        Info,
        Strings,
        General,
        SyncManagers,
        FMMU,
        TxPDO,
        RxPDO
    };

    class App
    {
    public:
        App();

        void render();

    private:
        void newFile();
        void openFile();
        void saveFile();
        void saveFileAs();

        void openFilePath(std::string const& path);
        void saveFilePath(std::string const& path);

        void renderMenuBar();
        void renderSidebar();
        void renderContentPanel();
        void renderHexPanel();
        void renderStatusBar();

        eeprom::SII sii_{};
        std::vector<uint8_t> serialized_;

        std::string file_path_;
        bool modified_{false};

        Category active_category_{Category::Info};
        MemoryEditor mem_edit_;
    };
}

#endif
