/*
 * JellyDrive5 — Google Drive source for Jelly5 (PS5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Configuration is read from /mnt/usb0/jellydrive5.json. OAuth tokens are
 * stored only in /download0/jelly5/google-drive-token.json, never in the repo.
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

    bool load_config(std::string *error);
    bool configured() const;
    bool has_saved_token();

    // Starts Google's Device Authorization Grant. The user completes sign-in
    // on a phone/computer using verification_url and user_code.
    bool begin_device_auth(std::string *verification_url, std::string *user_code, std::string *device_code,
                           int *expires_in, int *interval, std::string *error);
    bool finish_device_auth(const std::string &device_code, int expires_in, int interval,
                            std::string *error);

    bool list_shared_drives(std::vector<Drive> *out, std::string *error);
    bool list_folder(const std::string &drive_id, const std::string &parent_id,
                     std::vector<Entry> *out, std::string *error);
    bool stream_info(const Entry &entry, std::string *url, std::string *access_token,
                     std::string *error);

    static bool is_video_name(const std::string &name, const std::string &mime_type);
    static std::string tmdb_id_from_name(const std::string &name);

private:
    bool load_tokens();
    bool save_tokens();
    bool ensure_access_token(std::string *error);
    bool request_token(const std::string &form, std::string *body, std::string *error);
    std::string api_url(const std::string &path) const;

    mutable std::mutex m_lock;
    std::string m_client_id;
    std::string m_access_token;
    std::string m_refresh_token;
    long long m_expiry_epoch = 0;
    bool m_config_loaded = false;
};

} // namespace gdrive
