/*
 * JellyDrive5 — Google Drive source for Jelly5 (PS5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Imports an authorized Drive remote from /mnt/usb0/rclone.conf. Refreshed
 * OAuth tokens are cached under /download0/jelly5/google-drive-token.json.
 */
#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace gdrive {

struct Drive {
    std::string id;
    std::string name;
};

struct Entry {
    std::string id;
    std::string name;
    std::string mime_type;
    std::string size;
    std::string modified_time;
    std::string tmdb_id;
    bool folder = false;
    bool video = false;
};

class Client {
public:
    Client() = default;

    bool load_config(std::string *error); // legacy rclone config helper
    bool configured() const;
    bool has_saved_token();

    // Start a TV-style OAuth login through a short code shown on the console.
    bool begin_sign_in(std::string *url, std::string *pin, std::string *error);
    // Poll the sign-in service until the user completes authorization.
    bool poll_sign_in(bool *complete, std::string *error);

    bool list_shared_drives(std::vector<Drive> *out, std::string *error);
    bool list_folder(const std::string &drive_id, const std::string &parent_id,
                     std::vector<Entry> *out, std::string *error);
    bool stream_info(const Entry &entry, std::string *url, std::string *access_token,
                     std::string *error);

    static bool is_video_name(const std::string &name, const std::string &mime_type);
    static std::string tmdb_id_from_name(const std::string &name);

private:
    bool load_tokens();
    bool load_saved_signin_session();
    bool save_tokens();
    bool ensure_access_token(std::string *error);
    bool request_token(const std::string &form, std::string *body, std::string *error);
    std::string api_url(const std::string &path) const;

    mutable std::mutex m_lock;
    std::string m_client_id;
    std::string m_client_secret;
    std::string m_remote_name;
    std::string m_preferred_drive_id;
    std::string m_access_token;
    std::string m_refresh_token;
    long long m_expiry_epoch = 0;
    bool m_config_loaded = false;
    bool m_use_signin_server = false;
    std::string m_pending_pin;
    std::string m_pending_password;
};

} // namespace gdrive
