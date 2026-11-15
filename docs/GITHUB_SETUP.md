# Publishing NanoBook on GitHub

## One-time setup

1. Install Git and create a free account on github.com.
2. Tell Git who you are (once per computer):
   ```bash
   git config --global user.name  "Your Name"
   git config --global user.email "you@example.com"
   ```

## Publish

1. On github.com click **New repository**. Name it `nanobook`. Leave **"Add a README"** and
   **"Add .gitignore"** unticked (the project already has them). Create it.
2. In the project folder:
   ```bash
   git init
   git add .
   git status            # check: no build/ folder and no .exe files are listed
   git commit -m "NanoBook: order book and matching engine, tests, benchmark, docs"
   git branch -M main
   git remote add origin https://github.com/YOUR-USERNAME/nanobook.git
   git push -u origin main
   ```
3. In `README.md`, replace `YOUR-USERNAME` in the CI badge line with your GitHub username, commit and push.

## Check the automatic build (CI)

Open the repository's **Actions** tab. The workflow in `.github/workflows/ci.yml` builds the project with
CMake on Linux and Windows, runs all tests, and checks that the sample input still produces
`data/sample_trades.csv`.

* Green tick on both: your CMake setup works on two platforms. Mention this in the report.
* A red cross: open the failed step and read the log. A Windows (MSVC) compiler error is the most
  likely cause, because the code was developed and tested with g++ first. Fixing it is a good
  exercise; copy the error text if you need help.

## Keep a clean history from now on

* Make **small commits with clear messages** ("Add cancel for resting orders", not "changes").
* Do not rewrite history to make the project look as if it grew step by step. If a teacher asks, say
  the project was built in steps 1 to 7 (see `docs/VIVA_PREP.md`) and the first commit contains the result.
* For the submission, tag the exact version:
  ```bash
  git tag -a v1.0 -m "Final submission"
  git push origin v1.0
  ```
  Put the tag and the commit hash in the report appendix.

## Before you submit

* Replace the example benchmark numbers in `README.md` and `docs/optimization_log.md` with results from **your** machine.
* Fill in the two numbers in the pitch in `docs/VIVA_PREP.md`.
* Run `ctest` once more and make sure the working tree is committed (`git status` shows nothing to commit).
