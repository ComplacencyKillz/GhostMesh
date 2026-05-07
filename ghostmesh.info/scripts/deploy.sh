#!/usr/bin/env bash
set -euo pipefail

PARAMS_FILE="$(dirname "$0")/../parameters.ghostmeshinfo.yaml"

if [[ ! -f "$PARAMS_FILE" ]]; then
  echo "ERROR: parameters.ghostmeshinfo.yaml not found."
  echo "Copy parameters.template.yaml to parameters.ghostmeshinfo.yaml and fill in your SFTP credentials."
  exit 1
fi

read_param() {
  local val
  val=$(yq ".$1" "$PARAMS_FILE" | tr -d '"')
  # yq outputs "null" for empty YAML values — treat as empty so defaults apply
  [[ "$val" == "null" ]] && echo "" || echo "$val"
}

SFTP_HOST=$(read_param sftp_host)
SFTP_USER=$(read_param sftp_user)
SFTP_PASS=$(read_param sftp_pass)
SFTP_PATH=$(read_param sftp_remote_path)
SFTP_PORT=$(read_param sftp_port)

SFTP_PORT=${SFTP_PORT:-22}
SFTP_PATH=${SFTP_PATH:-/}

if [[ -z "$SFTP_HOST" || -z "$SFTP_USER" || -z "$SFTP_PASS" ]]; then
  echo "ERROR: sftp_host, sftp_user, and sftp_pass are required in parameters.ghostmeshinfo.yaml."
  exit 1
fi

echo "Building ghostmesh.info..."
npm run build

echo "Deploying to $SFTP_HOST:$SFTP_PATH ..."
lftp -u "$SFTP_USER","$SFTP_PASS" sftp://"$SFTP_HOST":"$SFTP_PORT" <<EOF
set sftp:auto-confirm yes
set net:max-retries 3
mirror --reverse --delete --verbose dist/ $SFTP_PATH
bye
EOF

echo "Deploy complete — ghostmesh.info is live."
