/*
 * JellyDrive5 — direct Google Drive browser for Jelly5 on PS5.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ui/drive_screen.h"

#include "app/spawn.h"
#include "gfx/gfx.h"
#include "nuvio_input.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ui {

DriveScreen::DriveScreen()
    : m_client(std::make_shared<gdrive::Client>()), m_state(std::make_shared<Shared>())
{
    m_state->status = "Google Drive direto (experimental)";
}

void DriveScreen::activate()
{
    {
        std::lock_guard<std::mutex> g(m_state->lock);
        m_state->selected = 0;
        m_state->error.clear();
        m_state->login_url.clear();
        m_state->login_pin.clear();
        m_state->signin_pending = false;
        m_state->loading = false;
    }
    if (m_client->has_saved_token()) {
        load_drives();
        return;
    }
    std::lock_guard<std::mutex> g(m_state->lock);
    m_state->mode = Shared::NeedConfig;
    m_state->status = "Pressione X para entrar com sua conta Google.";
}

void DriveScreen::load_drives()
{
    {
        std::lock_guard<std::mutex> g(m_state->lock);
        if (m_state->loading)
            return;
        m_state->loading = true;
        m_state->error.clear();
        m_state->status = "Listando Drives compartilhados...";
    }
    const auto state = m_state;
    const auto client = m_client;
    const bool started = jelly5::spawn([state, client] {
        std::vector<gdrive::Drive> drives;
        std::string error;
        const bool ok = client->list_shared_drives(&drives, &error);
        std::lock_guard<std::mutex> g(state->lock);
        state->loading = false;
        if (!ok) {
            state->error = error;
            state->status = "Erro ao consultar o Google Drive.";
            return;
        }
        state->drives = std::move(drives);
        state->mode = Shared::Drives;
        state->selected = 0;
        state->entries.clear();
        state->folder_ids.clear();
        state->folder_names.clear();
        state->status = state->drives.empty() ? "Nenhum Drive compartilhado foi encontrado."
                                             : "Selecione o Drive de equipe.";
    });
    if (!started) {
        std::lock_guard<std::mutex> g(m_state->lock);
        m_state->loading = false;
        m_state->error = "Não foi possível criar uma tarefa em segundo plano.";
    }
}

void DriveScreen::load_folder(const std::string &drive_id, const std::string &parent_id)
{
    {
        std::lock_guard<std::mutex> g(m_state->lock);
        if (m_state->loading)
            return;
        m_state->loading = true;
        m_state->error.clear();
        m_state->status = "Carregando arquivos do Drive...";
    }
    const auto state = m_state;
    const auto client = m_client;
    const bool started = jelly5::spawn([state, client, drive_id, parent_id] {
        std::vector<gdrive::Entry> entries;
        std::string error;
        const bool ok = client->list_folder(drive_id, parent_id, &entries, &error);
        std::lock_guard<std::mutex> g(state->lock);
        state->loading = false;
        if (!ok) {
            state->error = error;
            state->status = "Erro ao listar a pasta.";
            return;
        }
        state->entries = std::move(entries);
        state->selected = 0;
        state->mode = Shared::Folder;
        state->status = state->entries.empty() ? "Esta pasta não contém vídeos ou subpastas."
                                               : "X abre a pasta ou reproduz o vídeo.";
    });
    if (!started) {
        std::lock_guard<std::mutex> g(m_state->lock);
        m_state->loading = false;
        m_state->error = "Não foi possível criar uma tarefa em segundo plano.";
    }
}

Action DriveScreen::input(uint32_t p)
{
    Action a;
    Shared::Mode mode;
    bool loading = false;
    bool signin_pending = false;
    int selected = 0, count = 0;
    gdrive::Drive drive;
    gdrive::Entry entry;
    std::string drive_id, parent_id;
    {
        std::lock_guard<std::mutex> g(m_state->lock);
        mode = m_state->mode;
        loading = m_state->loading;
        signin_pending = m_state->signin_pending;
        selected = m_state->selected;
        count = mode == Shared::Drives ? (int)m_state->drives.size() :
                mode == Shared::Folder ? (int)m_state->entries.size() : 0;
        if (mode == Shared::Drives && selected >= 0 && selected < count)
            drive = m_state->drives[selected];
        if (mode == Shared::Folder && selected >= 0 && selected < count)
            entry = m_state->entries[selected];
        drive_id = m_state->drive_id;
        if (!m_state->folder_ids.empty())
            parent_id = m_state->folder_ids.back();
    }
    if (p & NUVIO_BTN_UP) {
        std::lock_guard<std::mutex> g(m_state->lock);
        if (count > 0)
            m_state->selected = (m_state->selected + count - 1) % count;
        return a;
    }
    if (p & NUVIO_BTN_DOWN) {
        std::lock_guard<std::mutex> g(m_state->lock);
        if (count > 0)
            m_state->selected = (m_state->selected + 1) % count;
        return a;
    }
    if (p & NUVIO_BTN_CIRCLE) {
        if (loading)
            return a;
        if (mode == Shared::Folder) {
            bool root = false;
            std::string next_parent, d;
            {
                std::lock_guard<std::mutex> g(m_state->lock);
                d = m_state->drive_id;
                if (m_state->folder_ids.size() > 1) {
                    m_state->folder_ids.pop_back();
                    if (!m_state->folder_names.empty())
                        m_state->folder_names.pop_back();
                    next_parent = m_state->folder_ids.back();
                } else {
                    root = true;
                    m_state->mode = Shared::Drives;
                    m_state->entries.clear();
                    m_state->selected = 0;
                    m_state->status = "Selecione o Drive de equipe.";
                }
            }
            if (!root)
                load_folder(d, next_parent);
        }
        return a;
    }
    if (!(p & NUVIO_BTN_CROSS) || loading)
        return a;
    if (mode == Shared::NeedConfig) {
        if (signin_pending)
            return a;
        {
            std::lock_guard<std::mutex> g(m_state->lock);
            m_state->loading = true;
            m_state->signin_pending = true;
            m_state->error.clear();
            m_state->login_url.clear();
            m_state->login_pin.clear();
            m_state->status = "Solicitando código de autorização...";
        }
        const auto state = m_state;
        const auto client = m_client;
        const bool started = jelly5::spawn([state, client] {
            std::string url, pin, error;
            if (!client->begin_sign_in(&url, &pin, &error)) {
                std::lock_guard<std::mutex> g(state->lock);
                state->loading = false;
                state->signin_pending = false;
                state->error = error;
                state->status = "Não foi possível iniciar o login. Pressione X para tentar novamente.";
                return;
            }
            {
                std::lock_guard<std::mutex> g(state->lock);
                state->loading = false;
                state->login_url = url;
                state->login_pin = pin;
                state->status = "Aguardando autorização no celular...";
            }
            for (int attempt = 0; attempt < 36; ++attempt) {
                bool complete = false;
                std::string poll_error;
                if (!client->poll_sign_in(&complete, &poll_error)) {
                    std::lock_guard<std::mutex> g(state->lock);
                    state->signin_pending = false;
                    state->error = poll_error;
                    state->status = "Falha no login. Pressione X para gerar outro código.";
                    return;
                }
                if (complete) {
                    std::vector<gdrive::Drive> drives;
                    std::string drives_error;
                    const bool ok = client->list_shared_drives(&drives, &drives_error);
                    std::lock_guard<std::mutex> g(state->lock);
                    state->signin_pending = false;
                    if (!ok) {
                        state->error = drives_error;
                        state->status = "Login feito, mas não foi possível listar os Drives.";
                        return;
                    }
                    state->drives = std::move(drives);
                    state->entries.clear();
                    state->folder_ids.clear();
                    state->folder_names.clear();
                    state->mode = Shared::Drives;
                    state->selected = 0;
                    state->error.clear();
                    state->status = state->drives.empty()
                        ? "Conta conectada, mas nenhum Drive compartilhado foi encontrado."
                        : "Conta conectada. Selecione o Drive de equipe.";
                    return;
                }
                if (attempt + 1 < 36)
                    std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            std::lock_guard<std::mutex> g(state->lock);
            state->signin_pending = false;
            state->error = "O código de autorização expirou ou demorou demais.";
            state->status = "Pressione X para gerar outro código.";
        });
        if (!started) {
            std::lock_guard<std::mutex> g(m_state->lock);
            m_state->loading = false;
            m_state->signin_pending = false;
            m_state->error = "O PS5 não conseguiu iniciar a tarefa de login.";
        }
        return a;
    }
    if (mode == Shared::Drives) {
        if (drive.id.empty())
            return a;
        {
            std::lock_guard<std::mutex> g(m_state->lock);
            m_state->drive_id = drive.id;
            m_state->drive_name = drive.name;
            m_state->folder_ids.assign(1, drive.id);
            m_state->folder_names.assign(1, drive.name);
            m_state->selected = 0;
        }
        load_folder(drive.id, drive.id);
        return a;
    }
    if (mode == Shared::Folder && !entry.id.empty()) {
        if (entry.folder) {
            {
                std::lock_guard<std::mutex> g(m_state->lock);
                m_state->folder_ids.push_back(entry.id);
                m_state->folder_names.push_back(entry.name);
            }
            load_folder(drive_id, entry.id);
            return a;
        }
        std::string url, token, error;
        if (!m_client->stream_info(entry, &url, &token, &error)) {
            std::lock_guard<std::mutex> g(m_state->lock);
            m_state->error = error;
            m_state->status = "Não foi possível abrir o vídeo.";
            return a;
        }
        a.kind = Action::PlayDirect;
        a.direct_url = url;
        a.direct_token = token;
        a.direct_title = entry.name;
        return a;
    }
    return a;
}

void DriveScreen::draw(double, float)
{
    gfx::begin_frame();
    gfx::fill({0, 0, gfx::W, gfx::H}, kBg);
    const gfx::TextStyle title_style{gfx::Bold, 42};
    const gfx::TextStyle status_style{gfx::Medium, 24};
    const gfx::TextStyle row_style{gfx::Regular, 25};
    glass_panel({64, 58, gfx::W - 128.f, gfx::H - 116.f}, 28, 0.97f, false);
    gfx::text(112, 124, "Google Drive", title_style, alpha(kText, 1.f), 0);

    std::string status, error, path, login_url, login_pin;
    std::vector<std::string> labels;
    int selected = 0;
    bool loading = false, signin_pending = false;
    {
        std::lock_guard<std::mutex> g(m_state->lock);
        status = m_state->status;
        error = m_state->error;
        login_url = m_state->login_url;
        login_pin = m_state->login_pin;
        signin_pending = m_state->signin_pending;
        selected = m_state->selected;
        loading = m_state->loading;
        if (m_state->mode == Shared::Drives) {
            for (const auto &d : m_state->drives)
                labels.push_back("  " + d.name);
        } else if (m_state->mode == Shared::Folder) {
            for (const auto &folder : m_state->folder_names)
                path += (path.empty() ? "" : " / ") + folder;
            for (const auto &e : m_state->entries)
                labels.push_back(std::string(e.folder ? "[+]  " : "[>]  ") + e.name);
        }
    }

    gfx::text(112, 180, status, status_style, alpha(error.empty() ? kText2 : 0xffff9b91u, 1.f), 0);
    if (!path.empty())
        gfx::text(112, 232, path, status_style, alpha(kText2, 1.f), 0);
    if (!login_url.empty()) {
        gfx::text(112, 238, "No celular conectado à mesma rede, abra:", status_style, alpha(kText2, 1.f), 0);
        gfx::text(112, 282, login_url, {gfx::Medium, 24}, 0xff8ed5ffu, 0);
        gfx::text(112, 340, "Código de autorização:", status_style, alpha(kText2, 1.f), 0);
        gfx::text(112, 396, login_pin, {gfx::Bold, 44}, kText, 0);
        gfx::text(112, 458, signin_pending
            ? "Depois de autorizar no Google, aguarde nesta tela."
            : "Pressione X para tentar novamente.", {gfx::Regular, 22}, alpha(kText2, 1.f), 0);
        gfx::text(112, 506, "Login via drive.signinserver.com; os vídeos são baixados diretamente do Google.",
                  {gfx::Regular, 18}, alpha(kText3, 1.f), 0);
    }
    if (!error.empty())
        gfx::text(112, login_url.empty() ? 340.f : 548.f, error, {gfx::Regular, 19}, 0xffff9b91u, 0);

    const float start_y = 294.f;
    const int rows = std::min<int>((int)labels.size(), 10);
    for (int i = 0; i < rows; ++i) {
        const float y = start_y + i * 48.f;
        const uint32_t color = i == selected ? 0xff8ed5ffu : kText2;
        if (i == selected)
            glass_panel({96, y - 30, gfx::W - 192.f, 42}, 12, 0.72f, false);
        gfx::text(120, y, labels[i], row_style, color, 0);
    }
    if (loading)
        gfx::text(gfx::W - 112, gfx::H - 74, "Carregando...", status_style, alpha(kText2, 1.f), 2);
    gfx::text(112, gfx::H - 74, "X: selecionar/reproduzir    O: voltar    ↑/↓: navegar",
              {gfx::Regular, 20}, alpha(kText3, 1.f), 0);
    gfx::end_frame();
    m_animating = false;
}

} // namespace ui
