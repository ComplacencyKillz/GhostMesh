# SKILL — GhostMesh Website Deploy

## Purpose

Use this skill when building, previewing, or deploying the **ghostmesh.info** website.

The site lives in `ghostmesh.info/` inside the GhostMesh repo. It is a static Astro site
deployed to IONOS shared hosting via SFTP using lftp.

---

## Credential File Location

```
~/repos/ghostmesh/ghostmesh.info/parameters.ghostmeshinfo.yaml
```

This file is **gitignored** and contains real IONOS SFTP credentials. Never commit it.

The template showing the expected structure is at:

```
~/repos/ghostmesh/ghostmesh.info/parameters.template.yaml
```

---

## Reading Credentials with yq

Use `yq` to parse the parameters file and export as environment variables before any
deploy operation:

```bash
PARAMS=~/repos/ghostmesh/ghostmesh.info/parameters.ghostmeshinfo.yaml

export SFTP_HOST=$(yq '.sftp_host' "$PARAMS")
export SFTP_USER=$(yq '.sftp_user' "$PARAMS")
export SFTP_PASS=$(yq '.sftp_pass' "$PARAMS")
export SFTP_PATH=$(yq '.sftp_remote_path' "$PARAMS")
export SFTP_PORT=$(yq '.sftp_port' "$PARAMS")
```

Verify they loaded (never print the password):

```bash
echo "Host: $SFTP_HOST  User: $SFTP_USER  Port: $SFTP_PORT  Path: $SFTP_PATH"
```

---

## Build

```bash
cd ~/repos/ghostmesh/ghostmesh.info
npm run build
```

Output lands in `ghostmesh.info/dist/`.

---

## Deploy

```bash
cd ~/repos/ghostmesh/ghostmesh.info
npm run deploy
```

The deploy script (`scripts/deploy.sh`) reads from `parameters.ghostmeshinfo.yaml`,
runs the build, and mirrors `dist/` to the IONOS web root via lftp SFTP.

Manual deploy if the script is unavailable:

```bash
lftp -u "$SFTP_USER","$SFTP_PASS" sftp://"$SFTP_HOST":"$SFTP_PORT" <<EOF
set sftp:auto-confirm yes
set net:max-retries 3
mirror --reverse --delete --verbose dist/ $SFTP_PATH
bye
EOF
```

---

## Dev Server

```bash
cd ~/repos/ghostmesh/ghostmesh.info
npm run dev
```

Runs Astro dev server at `http://localhost:4321`.

---

## Site Context

- **URL:** ghostmesh.info
- **Parent site:** complacencykillz.me (links here from SCENE SELECTION)
- **Role:** Sub-menu / project chapter — not a standalone main menu
- **Back link:** Every page has a RETURN TO MAIN MENU that points to complacencykillz.me
- **Visual style:** Continues the DVD menu aesthetic from the parent site — same palette,
  same CRT/scanline overlays, same GSAP transitions. No splash screen (user already
  passed through that at complacencykillz.me).

---

## Stack

| Layer | Choice |
|-------|--------|
| Framework | Astro (static) |
| Styling | Tailwind CSS v4 + custom CRT/glitch CSS |
| Animation | GSAP |
| Deploy | lftp SFTP → IONOS |

---

## File Structure (once initialized)

```
ghostmesh.info/
├── parameters.template.yaml    # tracked — placeholder values
├── parameters.ghostmeshinfo.yaml  # gitignored — real credentials
├── astro.config.mjs
├── package.json
├── tsconfig.json
├── scripts/
│   └── deploy.sh
├── public/
│   └── (static assets, video, fonts)
└── src/
    ├── layouts/
    │   └── Layout.astro        # shared CRT/scanline wrapper
    ├── styles/
    │   └── global.css          # shared effects
    └── pages/
        ├── index.astro         # GhostMesh sub-menu (SCENE SELECTION landing)
        ├── hardware.astro      # Build guide, BOM, wiring
        ├── software.astro      # FAP install, build
        ├── usecases.astro      # Grid-down, red team, SAR
        ├── docs.astro          # Links to GitHub documentation
        └── roadmap.astro       # Planned phases
```

---

## .gitignore Entry Required

Ensure `parameters.ghostmeshinfo.yaml` is gitignored:

```
ghostmesh.info/parameters.ghostmeshinfo.yaml
```
