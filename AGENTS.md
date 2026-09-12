# Repository Rules & AI Instructions
These rules apply for human and AI contributors.

1. **Prioritize UX and Maintainability Above All Else**
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
