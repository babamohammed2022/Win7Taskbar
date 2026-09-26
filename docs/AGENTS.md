# Repository Rules & AI Instructions

> **Archived project notice.** Maintenance was discontinued due to lack of available time. No maintainer-led updates, releases, or support are planned. These rules are retained as historical guidance for independent development in forks; repository access remains subject to its access settings.

These rules apply to human and AI contributors working on this source, including independent forks.

1. **Prioritize UX and Maintainability Above all Else**
   - The primary goal is that the software functions correctly, smoothly, and reliably on the user's system.
   - Keep the repository architecture clean, modern, and structured so external contributors can easily understand and modify the codebase without dealing with redundant folders or duplicate files.

2. **Self-Contained Execution (Zero Dependencies for the User)**
   - The user must be able to run the program simply by clicking the `.exe`.
   - The application must be published as a fully self-contained package. Do not rely on external prerequisites, specific installed .NET versions, or pre-installed runtimes on the target system.

3. **English-Only Documentation**
   - Write all documentation, instructions, comments, and issue templates strictly in English to ensure the project remains accessible and clear to the global open-source community.

4. **Mandatory Code Review After Every Change**
   - Thoroughly review every modification before committing.
   - Verify that build scripts, updated native DLLs, and managed code remain in sync, ensuring no broken features or obsolete assets are left behind.

5. **Humbleness and Tone**
   - Describe every change or fix plainly as a proposed solution to a problem.
   - Avoid sensational, dramatic, or self-congratulatory language in commit messages, pull requests, logs, and documentation.

6. **Repository Layout**
   - `compilation files/` holds every manual build/packaging script (`build.bat`,
     `build-release.ps1`, `publish.ps1`) and the asset converter `icons_to_base64.py`.
     Do not scatter new scripts elsewhere: reference them from there.
   - `src/`, `native/`, `Themes/`, `Resources/`, `Languages/` and `docs/` keep their
     position: move files inside them only when a task says so.
   - `build/publish.ps1` is a compatibility shim retained for existing scripts and forks.
     This repository has no active release workflow.
   - Generated assets (`native/src/TrayIconAssets.inc`, `native/src/BatteryAssets.inc`)
     are produced from the sources in `assets/icon-sources/` by
     `compilation files/icons_to_base64.py`. Never hand-edit the `.inc` files.
7. **Release status**

   No maintainer-led releases are planned for this archived repository.
   `v1.3.26-alpha` is the sole retained GitHub release and remains incomplete
   relative to the project's initial objectives. Release guidance may be adapted
   independently by fork maintainers.
