# JellyDrive5 — first test build

This branch adds an experimental Google Drive source to Jelly5. The intended design is direct access from the PS5: the app calls Google's Drive API, browses shared drives and streams the selected video to the existing native playback engine. No local Jellyfin server is used for the Google Drive source itself.

## Current alpha scope

- Google OAuth device authorization.
- Lists Google Shared Drives available to the authorized account.
- Browses folders and video files; deduplicates entries by Google Drive file ID.
- Recognizes TMDB IDs in names such as `Film (2022) [tmdbid-12345]`.
- Sends the video to the existing player as an authenticated HTTP stream, with the OAuth bearer token in the request header (not in the URL).

This is an early integration build. It does not yet download posters or fetch full TMDB metadata, and playback has not yet been verified on physical PS5 hardware. The app startup still follows the existing Jelly5 sign-in flow; fully bypassing that gate for Drive-only use is a follow-up step.

## Google OAuth setup

Google requires the application to provide its own OAuth client ID. Create an OAuth client intended for a TV / limited-input device in Google Cloud, enable the Google Drive API, and configure the consent screen for the account(s) to use the app. Use the read-only Drive scope.

Create a UTF-8 file on the root of a USB drive named `jellydrive5.json`:

```json
{
  "client_id": "YOUR_CLIENT_ID.apps.googleusercontent.com"
}
```

Connect the USB drive to the PS5 before starting Jelly5. The app reads `/mnt/usb0/jellydrive5.json`. The first time you open Google Drive, press X to start authorization. Visit the shown Google verification URL on a phone/computer, enter the displayed code and approve the account. The access and refresh tokens are saved under `/download0/jelly5/google-drive-token.json`.

The account must have access to the Shared Drives. The app only requests read-only scope; it does not upload, move, or delete Drive content.

## Security and test notes

- Never commit `jellydrive5.json`, OAuth tokens, `rclone.conf`, or other credentials into GitHub.
- Keep the config USB-connected for first configuration; tokens are saved to PS5 storage after authorization.
- Google API access and large-library performance depend on the account's Drive permissions, network and API quotas.
- This initial build will not appear as a release until GitHub Actions completes successfully. The installable package is expected as the `JellyDrive5-PS5-build` workflow artifact.
