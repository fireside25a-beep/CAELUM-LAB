# Downloads and publication

## Download the application

Choose the **`CAELUM-0.4.0-linux-x86_64-app.zip`** asset from this repository's GitHub Releases. Extract it and run `Start-CAELUM.sh` on Linux x86-64 with an X11 display. The example is explicitly labelled as a mathematical fixture. The workbench lets you edit the protocol and run location, and Run, Stop, Resume or Replay through the same native authority.

The application ZIP contains the program, example, license, notices, citation and format reference. GitHub presentation graphics, source, test binaries and research archives are excluded. Windows and macOS native execution remain unqualified.

Choose **`CAELUM-0.4.0-research-volume.zip`** for the complete source, SDK, tests, evidence, tools, figures and metadata. GitHub's **Code → Download ZIP** is a repository snapshot, not the application-only release asset.

## Prepare the release locally

`python3 verification/publication/package_release.py --output dist` validates the checked-in volume and creates both ZIPs, SHA-256 checksums and a package report. It uses the already qualified Linux binaries without rebuilding or relabelling the scientific evidence. It runs on a Mac as a packaging operation; this does not qualify execution of the Linux application on macOS.

## Upload with your own GitHub account

Upload the contents of the extracted repository folder as the repository root, including `.github/`, `.gitignore` and `.gitattributes`. Keep the existing data/evidence ZIPs in their documented locations. They are real research artifacts, not extra copies of the application. The largest current files are below [GitHub's 100 MiB per-file limit](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github); Git LFS is not required for this prepared volume.

Use your existing SSH Git workflow for this volume: the bundled Z3 executable exceeds GitHub's 25 MiB browser-upload limit, while remaining below the regular Git file limit. After choosing the repository name, create and push tag `v0.4.0` at the exact uploaded commit. In Actions, manually run **Prepare release** for that existing tag. The workflow checks, packages and attaches the assets to a **draft** release. Review it and publish it yourself. The workflow does not create or move tags, overwrite existing releases, or publish a release automatically.

The native CI and metadata checks run on pushes and pull requests. Included workflow configuration is not a claim that GitHub runners have already executed. The recorded local qualification remains in `RELEASE_STATUS.json` and `evidence/qualification/`.


## Website presentation in source downloads

Git source archives omit exactly `README.md` and `docs/assets/caelum-banner.svg` using `export-ignore`. Both remain tracked and visible on GitHub. All scientific figures remain. `DOWNLOAD_README.txt` introduces the downloaded source tree. The integrity verifier accepts the full checkout or precisely this two-file omission; other missing or modified payload files fail.
