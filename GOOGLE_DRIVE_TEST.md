# JellyDrive5 — direct Google Drive alpha

This branch builds a separate native PS5 homebrew app named **JellyDrive5**. In this build it opens directly into Google Drive, without requiring a Jellyfin/Emby account or a local media server.

## Included in this alpha

- Read a previously authorized Google Drive remote from `rclone.conf`.
- Refresh Google access tokens on the PS5 using the remote's OAuth client credentials.
- Browse Google Shared Drives, folders and common video files.
- Deduplicate entries by Google Drive file ID within each folder.
- Parse TMDB IDs from names such as `Film (2022) [tmdbid-12345].mkv`.
- Stream the selected video through Jelly5's native player using an Authorization header rather than putting the token into the URL.
- Use a separate PS5 title ID (`PPSA99508`) so the test app does not replace the regular Jelly5 installation.

This is a testable first alpha, not yet a full poster-based TMDB library. It browses the existing Drive folder tree and recognizes TMDB IDs; posters/details and a global recursive library index are follow-up work.

## 1. Prepare rclone on Windows

Install rclone from the [official downloads page](https://rclone.org/downloads/). The rclone configuration must use a **custom Google OAuth client ID and client secret**: these need to be present in the config so JellyDrive5 can refresh the token without relying on rclone's own embedded credentials.

1. In Google Cloud Console, create/select a project and enable the Google Drive API.
2. Configure the OAuth consent screen and add the Google account that will use the app as a test user if the app is still in testing.
3. Create OAuth credentials of type **Desktop app**. Save the client ID and client secret privately.
4. Run `rclone config` and create a remote named `gdrive`, type `drive`. Enter that custom client ID and client secret, choose the `drive.readonly` scope, then complete Google's browser sign-in.
5. When rclone asks whether to configure a Shared Drive (Team Drive), choose the appropriate option and select your Team Drive. Finish the setup.
6. Verify that the remote can see your files by running `rclone lsd gdrive:` and, for a known folder, `rclone lsjson gdrive:`. If your files do not appear here, correct rclone permissions before moving to the PS5.

Use a remote authenticated with read-only Drive scope. Avoid rclone's default embedded OAuth client credentials for this app: an exported config that omits `client_id` or `client_secret` cannot refresh its token independently.

## 2. Copy the config to USB

Run `rclone config file` to find the active `rclone.conf`. Copy that file to the **root** of a USB drive so the PS5 sees it as `/mnt/usb0/rclone.conf`.

If the file contains several Google Drive remotes and you want to select one explicitly, also create a UTF-8 file `jellydrive5.json` in the USB root with:

```json
{
  "remote": "gdrive"
}
```

The selector file is optional when the intended remote is named `gdrive` or there is only one Drive remote. The app prefers `team_drive` from the selected rclone remote when sorting Shared Drives.

**Security:** `rclone.conf` contains OAuth credentials and refresh tokens. Keep the USB private and never commit or send the config to GitHub or chat. The app copies refreshed tokens to `/download0/jelly5/google-drive-token.json` on the PS5.

## 3. Get the PS5 test build

Open [the JellyDrive5 GitHub Actions build](https://github.com/tales92/Jelly5-Gdrive/actions/workflows/jellydrive5-build.yml). Download the artifact named `JellyDrive5-PS5-build` from the latest successful run. It contains the installable package and build diagnostics.

Install the package using the homebrew method compatible with the PS5 jailbreak and firmware. The app has its own title ID, `PPSA99508`.

On first launch, JellyDrive5 should show your available Shared Drives. Choose the target Team Drive, navigate through the alphabetical folders, select a video and press X. The rclone config must be present before launching, or you can press X on the configuration message to reread it after fixing the USB.

## Alpha limitations

- No external media server is used for the Drive source.
- The browser shows files/folders and recognized TMDB IDs, but does not yet display TMDB posters or enriched metadata.
- Duplicate prevention currently uses Google Drive file ID within each folder; a global recursive media index is not implemented.
- GitHub Actions success verifies compilation and packaging only. Google token refresh, redirects, byte-range seeking, codecs and playback still require testing on the target PS5.
- Only read operations are implemented: JellyDrive5 does not upload, move or delete Drive files.
