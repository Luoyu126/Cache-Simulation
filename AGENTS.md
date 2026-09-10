# Repository Agent Instructions

## Git authentication

- Use SSH for GitHub operations and reuse the existing local SSH identity.
- The repository remote is `git@github.com:Luoyu126/Cache-Simulation.git`.
- Check repository-local `core.sshCommand`, SSH configuration, and existing local keys before concluding that authentication is unavailable.
- Never copy private keys into the repository or print their contents.

## Commit messages

- Write all new commit subjects and bodies in English.
- Use Conventional Commits: `<type>(<optional scope>): <description>`.
- Use appropriate types such as `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, or `chore`.
- Keep the subject concise and use an imperative description, for example: `chore: initialize cache simulation framework`.
