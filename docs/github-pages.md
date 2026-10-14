# Personal fork and GitHub Pages

The personal fork is https://github.com/mrmcgrain/SignalSweep. Development continues in the existing `G:\esp32\signalSweep\SignalSweep` folder.

- `origin` points to the personal fork.
- `upstream` points to https://github.com/B0z0dcl0wn/SignalSweep for future upstream updates.
- The companion app publishes at https://mrmcgrain.github.io/SignalSweep/app/.
- The site's root redirects to `./app/`.

## Deployment

Repository Settings > Pages uses **GitHub Actions**. The `Publish companion app` workflow runs on app/deployment changes pushed to `main` or through its manual dispatch button. It installs the locked npm dependencies, runs the existing companion checks, builds with Vite and deploys only the assembled static app. `deployment.json` records the source commit and repository for verification.

The relative Vite base supports the `/SignalSweep/app/` subpath. This website does not include the upstream firmware flasher or an Android APK download. The original combined release workflow is restricted to the upstream repository to avoid conflicting Pages publishers and unintended firmware/APK releases in this fork.

Local build:

```powershell
Set-Location G:\esp32\signalSweep\SignalSweep\app
npm ci
npm run build
```

`app/dist/` is generated output and stays out of source control. Private flash backups, hardware diagnostics, signing files, local board configuration and browser verification artifacts remain local. The XMC board setup still needs the ignored `firmware/devkitc.local.ini` when moving to another machine.

Browser hosting does not add Android-only phone scanning to the web app. BLE/USB use still depends on the browser's supported APIs, permissions and a connected board. Hardware behavior must be verified separately from deployment.

To redeploy, push app changes to the personal fork's `main`, or run `Publish companion app` in GitHub Actions. To roll back, revert the relevant source commit on `main` and run the workflow again.
