# Documentation Tooling

The documentation site is served by Docsify. Install the pinned local CLI
without running third-party post-install hooks:

```powershell
npm install
```

The repository's `docs/.npmrc` sets `ignore-scripts=true` to avoid the
`opencollective-postinstall` and Husky hooks used by some transitive packages.
Run `xmake docs` from the repository root to generate `build/docs/` and the
Doxygen reference at `build/docs/api/`.
