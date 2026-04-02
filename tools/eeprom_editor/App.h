#ifndef KICKCAT_EEPROM_EDITOR_APP_H
#define KICKCAT_EEPROM_EDITOR_APP_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "imgui.h"
#include "vendor/imgui_memory_editor.h"

#include "kickcat/AbstractSocket.h"
#include "kickcat/SIIParser.h"

namespace kickcat
{
    class Bus;
    class Link;
}

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

    enum class SlaveAction { Load, Flash };

    class App
    {
    public:
        App();
        ~App();

        void render();

    private:
        void newFile();
        void openFile();
        void saveFile();
        void saveFileAs();

        void openFilePath(std::string const& path);
        void saveFilePath(std::string const& path);

        // Device operations
        bool isConnected() const;
        bool isBusy() const;
        void connectToInterface(std::string const& name);
        void disconnect();
        void loadFromSlave(int slave_index);
        void flashToSlave(int slave_index);
        void joinWorker();
        void finalizeWorker();

        // Rendering
        void renderMenuBar();
        void renderSidebar();
        void renderContentPanel();
        void renderHexPanel();
        void renderStatusBar();
        void renderConnectDialog();
        void renderSlaveDialog();
        bool renderSlaveTable();  // returns true on double-click
        void renderPrivilegeDialog();

        // EEPROM state
        eeprom::SII sii_{};
        std::vector<uint8_t> serialized_;
        std::string file_path_;
        bool modified_{false};
        Category active_category_{Category::Info};
        MemoryEditor mem_edit_;

        // EtherCAT connection
        std::shared_ptr<Link> link_;
        std::unique_ptr<Bus> bus_;
        std::string connected_interface_;

        // Dialog state
        bool show_connect_dialog_{false};
        bool show_slave_dialog_{false};
        SlaveAction slave_action_{SlaveAction::Load};
        int selected_slave_index_{-1};
        std::string device_error_;
        std::vector<NetworkInterface> cached_interfaces_;

        // Privilege escalation
        bool show_privilege_dialog_{false};
        bool privilege_granted_{false};
        std::string privilege_error_;
        std::atomic<bool> needs_privilege_escalation_{false};

        // Background worker state
        std::thread worker_;
        std::atomic<float> worker_progress_{0.0f};
        std::atomic<bool> worker_done_{false};
        std::mutex worker_status_mutex_;
        std::string worker_status_;
        std::string worker_error_;
    };
}

#endif
