#!/usr/bin/env bash
# Scaffold a new MADR v3 ADR with the next available number.
#
# Usage: ./scripts/adr-new.sh "Short title in imperative form"
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Usage: $0 \"Short title in imperative form\""
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
adr_dir="${repo_root}/docs/adr"
template="${adr_dir}/template.md"

if [ ! -d "${adr_dir}" ]; then
  echo "ADR directory missing: ${adr_dir}"
  exit 1
fi

if [ ! -f "${template}" ]; then
  echo "Template missing: ${template}"
  exit 1
fi

title="$1"

# Derive slug: lowercase, replace non-alphanumeric runs with single hyphen,
# trim leading and trailing hyphens.
slug="$(printf '%s' "${title}" \
  | tr '[:upper:]' '[:lower:]' \
  | sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//')"

if [ -z "${slug}" ]; then
  echo "Could not derive slug from title."
  exit 1
fi

# Highest existing 4-digit ADR number; new = highest + 1.
last_num=$(ls -1 "${adr_dir}" \
  | grep -E '^[0-9]{4}-' \
  | sort -r \
  | head -n1 \
  | cut -c1-4 \
  || echo "0000")

new_num=$(printf '%04d' $((10#${last_num} + 1)))
new_file="${adr_dir}/${new_num}-${slug}.md"

if [ -e "${new_file}" ]; then
  echo "File already exists: ${new_file}"
  exit 1
fi

today="$(date -u +%Y-%m-%d)"

sed \
  -e "s/^status:.*/status: \"Proposed\"/" \
  -e "s/^date:.*/date: \"${today}\"/" \
  -e "s/^# NNNN\. Title in imperative form$/# ${new_num}. ${title}/" \
  "${template}" > "${new_file}"

echo "Created ${new_file}"
echo "Remember to add it to the index in docs/adr/README.md."
