# Git initialization and repository hygiene — qVENTUS

Repository: `ventus-cc`. CloudCompare plugin: `qVENTUS`; menu display: `VENTUS`.

## 1. Add the setup files without overwriting source

Extract the **Git setup overlay** at the root of your existing working copy
(`~/dev/ventus-cc`). It contains only `.gitignore`, these documents, and the
Git-safety tools. It does **not** replace the local qVENTUS C++/Qt source.

## 2. Initialize and stage safely

```bash
cd ~/dev/ventus-cc
bash tools/setup_git_repository.sh
```

The script initializes `main` only if necessary, stages non-ignored files,
checks staged paths for accidental dataset inclusion, and shows status.
**It never commits, deletes files, connects a remote, or pushes.**

Check what would go into your first commit:

```bash
git status --short
git diff --cached --stat
git diff --cached --name-only
python3 tools/check_repo_hygiene.py --staged
```

Only after review:

```bash
git commit -m "Initial qVENTUS development baseline with native CLI"
```

If Git requests your identity, set it locally (use your own details):

```bash
git config user.name "Your Name"
git config user.email "you@example.com"
```

## 3. Connect GitHub when the remote repository exists

Create a **blank** repository named `ventus-cc` under your GitHub account,
without auto-generating a README or .gitignore. Then:

```bash
cd ~/dev/ventus-cc
git remote add origin git@github.com:dabasler/ventus-cc.git
git push -u origin main
```

Or, if GitHub CLI (`gh`) is installed and authenticated and you have not
created the remote repository yet:

```bash
gh repo create dabasler/ventus-cc --public --source=. --remote=origin --push
```

Do not run both remote-creation approaches. Neither the remote repository nor
any push is performed automatically by this setup kit.

## 4. Files that belong in Git

Commit: `qVENTUS/`, `core/`, `cli/`, `tools/`, `tests/` source and tiny
synthetic fixtures, `docs/`, `.github/`, `CMakeLists.txt`, `README.md`,
`DEVELOPMENT.md`, `VERSION`, and `LICENSE`.

Never commit: extracted Wytham scans, other point-cloud datasets, BIN outputs,
CSV point outputs, build directories, regression output, archives, or `results/`.
See [WYTHAM_TEST_DATA.md](WYTHAM_TEST_DATA.md) for dataset provenance and test
reproduction steps. Large files should be stored externally—not in Git LFS
by default for this project.

## 5. If Git had already tracked a dataset

`.gitignore` does **not** untrack previously committed files.
First identify any offenders:

```bash
python3 tools/check_repo_hygiene.py --tracked
```

For a file which must become locally untracked (example only):

```bash
git rm --cached -- path/to/previously-tracked-data.ply
```

For an accidentally tracked directory:

```bash
git rm -r --cached -- tests/data/
```

`--cached` keeps the local copy. **If large data already entered Git history,**
removing it in a later commit does not remove it from old commits. Review and
rewrite history with `git filter-repo` only after making a backup and
coordinating with other contributors. Avoid force-pushing shared history casually.

## 6. Recheck before future commits

```bash
git add -A
python3 tools/check_repo_hygiene.py --staged
git diff --cached --stat
git commit -m "Describe change"
```

You can keep your CloudCompare symlink workflow unchanged:

```text
~/dev/CloudCompare/plugins/core/Standard/qVENTUS
  -> ~/dev/ventus-cc/qVENTUS
```

The installed CloudCompare binary and separate build directories are not part
of the `ventus-cc` repository.
