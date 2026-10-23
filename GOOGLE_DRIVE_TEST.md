# JellyDrive5 — direct Google Drive alpha

This branch builds a separate native PS5 homebrew app named **JellyDrive5**. In this build, its start screen goes directly to Google Drive: no Jellyfin/Emby account or local media server is required.

## Included in this alpha

- Google OAuth device sign-in (read-only Google Drive scope).
- Browse Google Shared Drives and navigate folders.
- List common video files and deduplicate entries by Google Drive file ID within each folder.
- Parse TMDB IDs in names such as `Film (2022) [tmdbid-12345].mkv`.
- Send selected videos to Jelly5's existing native player, with the Google OAuth bearer token passed as an HTTP header rather than embedded in the URL.
- A separate PS5 title ID (`PPSA99508`) so the test app does not overwrite the normal Jelly5 installation.

This is a testable first alpha, not yet a full poster-based TMDB library. The folder browser uses the existing Drive folder structure; TMDB posters/details, recursive whole-library scan, and physical-console playback verification are still follow-up work.

## Google OAuth setup

Google requires an OAuth client ID. In Google Cloud, create a client for a TV / limited-input device, enable Google Drive API, and configure the consent screen/account for testing. The app requests read-only Drive access.

Create a UTF-8 file at the root of a FAT32/exFAT USB drive named `jellydrive5.json`:

```json
{
  "client_id": "YOUR_CLIENT_ID.apps.googleusercontent.com"
}
```

Connect the USB drive to the PS5 before launching JellyDrive5. The app reads `/mnt/usb0/jellydrive5.json`. On the Google Drive screen, press X. Open the displayed verification URL on your phone/computer, enter the displayed code and approve the account. The app then lists available Shared Drives.

OAuth tokens are stored under `/download0/jelly5/google-drive-token.json` on the PS5. Keep them private. No tokens, Google credentials or `rclone.conf` belong in GitHub.

## Test build

Open the [GitHub Actions build](https://github.com/tales92/Jelly5-Gdrive/actions/workflows/jellydrive5-build.yml). Download the artifact named `JellyDrive5-PS5-build` from the latest successful run. It contains the installable package and diagnostics.

Install the app using the homebrew package-install method appropriate for the console's jailbreak/firmware. The build is unsigned in the retail-PlayStation sense and is intended for the compatible homebrew environment only.

## Alpha limitations

- The app browses the Drive and can open a selected video, but does not yet draw TMDB posters or enriched metadata.
- Duplicate prevention uses the Google Drive file ID for entries in each folder. A global recursive library index is not implemented yet.
- The first build must be tested on the target PS5 for Google OAuth, redirects/byte-range seeking and codecs; the GitHub Actions build alone is not proof of successful console playback.
- Only read operations are implemented: the app does not upload, move or delete Drive files.
