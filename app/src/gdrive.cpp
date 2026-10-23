/*
 * JellyDrive5 — Google Drive API access on the PS5.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "gdrive.h"
#include "jf/jf_http.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "cJSON.h"
}

namespace gdrive {
namespace {

constexpr const char *kConfigPath = "/mnt/usb0/rclone.conf";
constexpr const char *kSelectorPath = "/mnt/usb0/jellydrive5.json";
constexpr const char *kTokenPath = "/download0/jelly5/google-drive-token.json";
constexpr const char *kDeviceCodeUrl = "https://oauth2.googleapis.com/device/code";
constexpr const char *kTokenUrl = "https://oauth2.googleapis.com/token";
constexpr const char *kDriveApi = "https://www.googleapis.com/drive/v3";

std::string read_file(const char *path, size_t cap = 1u << 20)
{
    std::string data;
    if (FILE *f = std::fopen(path, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && data.size() < cap)
            data.append(buf, std::min(n, cap - data.size()));
        std::fclose(f);
    }
    return data;
}

std::string json_string(const cJSON *j, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

long long json_int(const cJSON *j, const char *key, long long fallback = 0)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsNumber(v) ? (long long)v->valuedouble : fallback;
}

std::string lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string url_encode(const std::string &s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string form_value(const std::string &key, const std::string &value)
{
    return url_encode(key) + "=" + url_encode(value);
}

bool write_file_atomic(const char *path, const std::string &data)
{
    mkdir("/download0/jelly5", 0700);
    const std::string temp = std::string(path) + ".tmp";
    FILE *f = std::fopen(temp.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fflush(f);
    std::fclose(f);
    if (!ok || std::rename(temp.c_str(), path) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    chmod(path, 0600);
    return true;
}

std::string bearer(const std::string &token)
{
    return "Authorization: Bearer " + token;
}

bool request_ok(const jf::HttpResponse &r, std::string *error, const char *what)
{
    if (r.ok())
        return true;
    if (error) {
        *error = what;
        if (r.status)
            *error += " (HTTP " + std::to_string(r.status) + ")";
        if (!r.error.empty())
            *error += ": " + r.error;
    }
    return false;
}

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace((unsigned char)s.front()))
        s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))
        s.pop_back();
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

using IniRemote = std::map<std::string, std::string>;

std::map<std::string, IniRemote> parse_rclone_config(const std::string &text)
{
    std::map<std::string, IniRemote> remotes;
    std::istringstream stream(text);
    std::string line, section;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const size_t eq = line.find('=');
        if (section.empty() || eq == std::string::npos)
            continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));
        if (!key.empty())
            remotes[section][key] = value;
    }
    return remotes;
}

} // namespace

bool Client::load_config(std::string *error)
{
    std::lock_guard<std::mutex> g(m_lock);
    if (m_config_loaded && !m_client_id.empty() && !m_client_secret.empty() && !m_refresh_token.empty())
        return true;

    const std::string ini_text = read_file(kConfigPath, 4u << 20);
    if (ini_text.empty()) {
        if (error)
            *error = std::string("Copie o rclone.conf para a raiz do USB: ") + kConfigPath;
        return false;
    }
    const auto remotes = parse_rclone_config(ini_text);

    std::string preferred;
    cJSON *selector = cJSON_Parse(read_file(kSelectorPath, 65536).c_str());
    if (selector) {
        preferred = json_string(selector, "remote");
        cJSON_Delete(selector);
    }
    if (preferred.empty() && remotes.find("gdrive") != remotes.end())
        preferred = "gdrive";

    auto valid = [](const IniRemote &r) {
        return r.find("type") != r.end() && r.at("type") == "drive" &&
               r.find("token") != r.end() && !r.at("token").empty();
    };
    auto chosen = remotes.end();
    if (!preferred.empty()) {
        const auto it = remotes.find(preferred);
        if (it != remotes.end() && valid(it->second))
            chosen = it;
    }
    if (chosen == remotes.end()) {
        for (auto it = remotes.begin(); it != remotes.end(); ++it) {
            if (valid(it->second)) {
                chosen = it;
                break;
            }
        }
    }
    if (chosen == remotes.end()) {
        if (error)
            *error = "O rclone.conf não contém um remote 'type = drive' com token. Configure e autorize o Google Drive no Windows.";
        return false;
    }

    const IniRemote &r = chosen->second;
    m_remote_name = chosen->first;
    m_client_id = r.count("client_id") ? r.at("client_id") : "";
    m_client_secret = r.count("client_secret") ? r.at("client_secret") : "";
    m_preferred_drive_id = r.count("team_drive") ? r.at("team_drive") : "";

    if (m_client_id.empty() || m_client_secret.empty()) {
        if (error)
            *error = "O remote rclone precisa de client_id e client_secret próprios para renovar o token. Configure-os antes de autorizar o remote.";
        m_config_loaded = false;
        return false;
    }

    cJSON *token = cJSON_Parse(r.at("token").c_str());
    if (!token) {
        if (error)
            *error = "O campo token do rclone.conf não contém JSON válido.";
        m_config_loaded = false;
        return false;
    }
    m_access_token = json_string(token, "access_token");
    m_refresh_token = json_string(token, "refresh_token");
    m_expiry_epoch = 0; /* Refresh on first use; subsequent expiry is cached locally. */
    cJSON_Delete(token);
    if (m_refresh_token.empty()) {
        if (error)
            *error = "O token do remote não contém refresh_token. Execute 'rclone config reconnect " + m_remote_name + ":' no Windows.";
        m_config_loaded = false;
        return false;
    }

    m_config_loaded = true;
    /* A refreshed token is cached between launches, but only for the same remote/client. */
    load_tokens();
    if (m_refresh_token.empty()) {
        if (error)
            *error = "Não foi possível carregar o refresh token do remote.";
        m_config_loaded = false;
        return false;
    }
    return true;
}

bool Client::configured() const
{
    std::lock_guard<std::mutex> g(m_lock);
    return m_config_loaded && !m_client_id.empty();
}

bool Client::load_tokens()
{
    const std::string text = read_file(kTokenPath, 65536);
    cJSON *root = cJSON_Parse(text.c_str());
    if (!root)
        return false;
    const std::string old_client = json_string(root, "client_id");
    const std::string old_remote = json_string(root, "remote");
    if ((!old_client.empty() && old_client != m_client_id) ||
        (!old_remote.empty() && old_remote != m_remote_name)) {
        cJSON_Delete(root);
        return false;
    }
    const std::string access = json_string(root, "access_token");
    const std::string refresh = json_string(root, "refresh_token");
    if (!access.empty())
        m_access_token = access;
    if (!refresh.empty())
        m_refresh_token = refresh;
    m_expiry_epoch = json_int(root, "expires_at", 0);
    cJSON_Delete(root);
    return !m_access_token.empty() || !m_refresh_token.empty();
}

bool Client::has_saved_token()
{
    if (!load_config(nullptr))
        return false;
    std::lock_guard<std::mutex> g(m_lock);
    return !m_refresh_token.empty();
}

bool Client::save_tokens()
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return false;
    cJSON_AddStringToObject(root, "client_id", m_client_id.c_str());
    cJSON_AddStringToObject(root, "remote", m_remote_name.c_str());
    cJSON_AddStringToObject(root, "access_token", m_access_token.c_str());
    cJSON_AddStringToObject(root, "refresh_token", m_refresh_token.c_str());
    cJSON_AddNumberToObject(root, "expires_at", (double)m_expiry_epoch);
    char *raw = cJSON_PrintUnformatted(root);
    const std::string text = raw ? raw : "";
    if (raw)
        std::free(raw);
    cJSON_Delete(root);
    return !text.empty() && write_file_atomic(kTokenPath, text);
}

bool Client::request_token(const std::string &form, std::string *body, std::string *error)
{
    const jf::HttpResponse r = jf::http_request(
        "POST", kTokenUrl,
        {"Content-Type: application/x-www-form-urlencoded", "Accept: application/json",
         "User-Agent: JellyDrive5/0.1"}, form, 20);
    if (!r.ok()) {
        if (error) {
            *error = "Google OAuth";
            if (r.status)
                *error += " (HTTP " + std::to_string(r.status) + ")";
            if (!r.error.empty())
                *error += ": " + r.error;
        }
        return false;
    }
    if (body)
        *body = r.body;
    return true;
}

bool Client::begin_device_auth(std::string *verification_url, std::string *user_code, std::string *device_code,
                               int *expires_in, int *interval, std::string *error)
{
    if (!load_config(error))
        return false;
    const std::string form = form_value("client_id", m_client_id) + "&" +
        form_value("scope", "https://www.googleapis.com/auth/drive.readonly");
    const jf::HttpResponse r = jf::http_request(
        "POST", kDeviceCodeUrl,
        {"Content-Type: application/x-www-form-urlencoded", "Accept: application/json",
         "User-Agent: JellyDrive5/0.1"}, form, 20);
    if (!request_ok(r, error, "Não foi possível iniciar a autenticação Google"))
        return false;
    cJSON *root = cJSON_Parse(r.body.c_str());
    if (!root) {
        if (error) *error = "Resposta de autenticação do Google inválida.";
        return false;
    }
    const std::string device = json_string(root, "device_code");
    const std::string code = json_string(root, "user_code");
    std::string url = json_string(root, "verification_url");
    if (url.empty())
        url = json_string(root, "verification_uri");
    const int expires = (int)json_int(root, "expires_in", 600);
    const int every = (int)json_int(root, "interval", 5);
    cJSON_Delete(root);
    if (device.empty() || code.empty() || url.empty()) {
        if (error) *error = "Google não forneceu o código de dispositivo. Confira o client_id e o tipo do OAuth.";
        return false;
    }
        if (verification_url) *verification_url = url;
    if (user_code) *user_code = code;
    if (device_code) *device_code = device;
    if (expires_in) *expires_in = expires;
    if (interval) *interval = every;
    return true;
}

bool Client::finish_device_auth(const std::string &device_code, int expires_in, int interval,
                               std::string *error)
{
    if (!load_config(error))
        return false;
    if (interval < 1) interval = 5;
    const long long deadline = (long long)std::time(nullptr) + std::max(60, expires_in);
    int pending_count = 0;
    while ((long long)std::time(nullptr) < deadline) {
        usleep((useconds_t)interval * 1000000u);
        const std::string form = form_value("client_id", m_client_id) + "&" +
            form_value("device_code", device_code) + "&" +
            form_value("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
        std::string body;
        std::string ignored;
        if (!request_token(form, &body, &ignored)) {
            // Google returns HTTP 400 while the user has not approved yet. The
            // console HTTP shim deliberately hides error bodies, so treat 400
            // as pending until the device-code expiry and stop on other errors.
            if (ignored.find("400") == std::string::npos) {
                if (error) *error = ignored;
                return false;
            }
            if (++pending_count % 4 == 0)
                interval = std::min(interval + 1, 15);
            continue;
        }
        cJSON *root = cJSON_Parse(body.c_str());
        if (!root) {
            if (error) *error = "Resposta de token Google inválida.";
            return false;
        }
        const std::string access = json_string(root, "access_token");
        const std::string refresh = json_string(root, "refresh_token");
        const long long ttl = json_int(root, "expires_in", 3600);
        cJSON_Delete(root);
        if (access.empty()) {
            if (error) *error = "Google respondeu sem access_token.";
            return false;
        }
        {
            std::lock_guard<std::mutex> g(m_lock);
            m_access_token = access;
            if (!refresh.empty())
                m_refresh_token = refresh;
            m_expiry_epoch = (long long)std::time(nullptr) + std::max(60LL, ttl);
            if (!save_tokens()) {
                if (error) *error = "Token obtido, mas não foi possível gravá-lo em /download0/jelly5.";
                return false;
            }
        }
        return true;
    }
    if (error) *error = "O código de autorização Google expirou. Tente conectar novamente.";
    return false;
}

bool Client::ensure_access_token(std::string *error)
{
    if (!load_config(error))
        return false;
    std::lock_guard<std::mutex> g(m_lock);
    if (m_access_token.empty() && m_refresh_token.empty())
        load_tokens();
    const long long now = (long long)std::time(nullptr);
    if (!m_access_token.empty() && m_expiry_epoch > now + 120)
        return true;
    if (m_refresh_token.empty()) {
        if (error) *error = "A conta Google não está autorizada. Pressione X em Google Drive para conectar.";
        return false;
    }
    std::string form = form_value("client_id", m_client_id) + "&" +
        form_value("client_secret", m_client_secret) + "&" +
        form_value("refresh_token", m_refresh_token) + "&" +
        form_value("grant_type", "refresh_token");
    std::string body;
    std::string request_error;
    if (!request_token(form, &body, &request_error)) {
        m_access_token.clear();
        if (error) *error = "Não foi possível renovar o token Google: " + request_error;
        return false;
    }
    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        if (error) *error = "Resposta de renovação Google inválida.";
        return false;
    }
    const std::string access = json_string(root, "access_token");
    const long long ttl = json_int(root, "expires_in", 3600);
    cJSON_Delete(root);
    if (access.empty()) {
        if (error) *error = "Google não retornou um access_token renovado.";
        return false;
    }
    m_access_token = access;
    m_expiry_epoch = now + std::max(60LL, ttl);
    if (!save_tokens()) {
        if (error) *error = "O token renovado não pôde ser salvo.";
        return false;
    }
    return true;
}

std::string Client::api_url(const std::string &path) const
{
    return std::string(kDriveApi) + path;
}

bool Client::list_shared_drives(std::vector<Drive> *out, std::string *error)
{
    if (!out || !ensure_access_token(error))
        return false;
    std::vector<Drive> result;
    std::string page;
    for (int pass = 0; pass < 100; pass++) {
        std::string url = api_url("/drives?pageSize=100&fields=nextPageToken,drives(id,name)");
        if (!page.empty())
            url += "&pageToken=" + url_encode(page);
        const std::string token = m_access_token;
        const jf::HttpResponse r = jf::http_request("GET", url,
            {bearer(token), "Accept: application/json", "User-Agent: JellyDrive5/0.1"}, "", 30);
        if (!request_ok(r, error, "Não foi possível listar os Drives compartilhados"))
            return false;
        cJSON *root = cJSON_Parse(r.body.c_str());
        if (!root) { if (error) *error = "Resposta inválida ao listar Drives."; return false; }
        const cJSON *drives = cJSON_GetObjectItemCaseSensitive(root, "drives");
        cJSON *d = nullptr;
        cJSON_ArrayForEach(d, drives) {
            Drive drive;
            drive.id = json_string(d, "id");
            drive.name = json_string(d, "name");
            if (!drive.id.empty())
                result.push_back(drive);
        }
        page = json_string(root, "nextPageToken");
        cJSON_Delete(root);
        if (page.empty())
            break;
    }
    *out = std::move(result);
    return true;
}

bool Client::list_folder(const std::string &drive_id, const std::string &parent_id,
                         std::vector<Entry> *out, std::string *error)
{
    if (!out || drive_id.empty() || !ensure_access_token(error))
        return false;
    std::vector<Entry> result;
    std::set<std::string> ids;
    std::string page;
    const std::string parent = parent_id.empty() ? drive_id : parent_id;
    const std::string q = "'" + parent + "' in parents and trashed = false";
    for (int pass = 0; pass < 100; pass++) {
        std::string url = api_url("/files?corpora=drive&driveId=" + url_encode(drive_id) +
            "&includeItemsFromAllDrives=true&supportsAllDrives=true&pageSize=1000&orderBy=name" +
            std::string("&q=") + url_encode(q) +
            "&fields=nextPageToken,files(id,name,mimeType,size,modifiedTime,thumbnailLink,webViewLink,parents)");
        if (!page.empty())
            url += "&pageToken=" + url_encode(page);
        const std::string token = m_access_token;
        const jf::HttpResponse r = jf::http_request("GET", url,
            {bearer(token), "Accept: application/json", "User-Agent: JellyDrive5/0.1"}, "", 30);
        if (!request_ok(r, error, "Não foi possível listar a pasta do Drive compartilhado"))
            return false;
        cJSON *root = cJSON_Parse(r.body.c_str());
        if (!root) { if (error) *error = "Resposta inválida ao listar arquivos do Drive."; return false; }
        const cJSON *files = cJSON_GetObjectItemCaseSensitive(root, "files");
        cJSON *f = nullptr;
        cJSON_ArrayForEach(f, files) {
            Entry e;
            e.id = json_string(f, "id");
            e.name = json_string(f, "name");
            e.mime_type = json_string(f, "mimeType");
            e.size = json_string(f, "size");
            e.modified_time = json_string(f, "modifiedTime");
            e.folder = e.mime_type == "application/vnd.google-apps.folder";
            e.video = is_video_name(e.name, e.mime_type);
            e.tmdb_id = tmdb_id_from_name(e.name);
            if (!e.id.empty() && ids.insert(e.id).second && (e.folder || e.video))
                result.push_back(e);
        }
        page = json_string(root, "nextPageToken");
        cJSON_Delete(root);
        if (page.empty())
            break;
    }
    std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) {
        if (a.folder != b.folder)
            return a.folder > b.folder;
        return lower(a.name) < lower(b.name);
    });
    *out = std::move(result);
    return true;
}

bool Client::stream_info(const Entry &entry, std::string *url, std::string *access_token,
                         std::string *error)
{
    if (entry.id.empty() || entry.folder || !url || !access_token) {
        if (error) *error = "O item selecionado não é um vídeo.";
        return false;
    }
    if (!ensure_access_token(error))
        return false;
    *url = api_url("/files/" + url_encode(entry.id) + "?alt=media");
    *access_token = m_access_token;
    return true;
}

bool Client::is_video_name(const std::string &name, const std::string &mime_type)
{
    if (mime_type.find("video/") == 0)
        return true;
    const std::string n = lower(name);
    const char *extensions[] = {".mkv", ".mp4", ".m4v", ".mov", ".avi", ".webm",
                                ".mpg", ".mpeg", ".ts", ".m2ts", ".mts", ".wmv", ".flv"};
    for (const char *ext : extensions) {
        const size_t len = std::char_traits<char>::length(ext);
        if (n.size() >= len && n.compare(n.size() - len, len, ext) == 0)
            return true;
    }
    return false;
}

std::string Client::tmdb_id_from_name(const std::string &name)
{
    const std::string n = lower(name);
    const char *markers[] = {"[tmdbid-", "(tmdbid-", "tmdbid-"};
    for (const char *marker : markers) {
        size_t pos = n.find(marker);
        while (pos != std::string::npos) {
            size_t i = pos + std::char_traits<char>::length(marker);
            const size_t begin = i;
            while (i < n.size() && n[i] >= '0' && n[i] <= '9')
                i++;
            if (i > begin)
                return name.substr(begin, i - begin);
            pos = n.find(marker, pos + 1);
        }
    }
    return "";
}

} // namespace gdrive
