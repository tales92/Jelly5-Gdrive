/*
 * JellyDrive5 — direct Google Drive browser for Jelly5 on PS5.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "gdrive.h"
#include "ui/screen.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace ui {

class DriveScreen : public Screen {
public:
    DriveScreen();
    ~DriveScreen() override = default;

    void activate() override;
    Action input(uint32_t pressed) override;
    void draw(double now, float dt) override;
    bool animating() const override { return m_animating; }
    float nav_alpha() const override { return 1.f; }

private:
    struct Shared {
        enum Mode { NeedConfig, Drives, Folder } mode = NeedConfig;
        std::mutex lock;
        bool loading = false;
        bool signin_pending = false;
        std::string status;
        std::string error;
        std::string login_url;
        std::string login_pin;
        std::string drive_id;
        std::string drive_name;
        std::vector<gdrive::Drive> drives;
        std::vector<gdrive::Entry> entries;
        std::vector<std::string> folder_ids;
        std::vector<std::string> folder_names;
        int selected = 0;
        unsigned generation = 0;
    };

    void load_drives();
    void load_folder(const std::string &drive_id, const std::string &parent_id);
    std::shared_ptr<gdrive::Client> m_client;
    std::shared_ptr<Shared> m_state;
    bool m_animating = false;
};

} // namespace ui
