#!/bin/sh

# Update all submodules to their latest commit on their respective branches
git submodule update --init --recursive

# Loop through each submodule, checkout the main branch if it exists, and pull changes
git submodule foreach '
  echo "Processing submodule $name"
  if git show-ref --verify --quiet refs/heads/main; then
    git checkout main
    git pull origin main
    ./update.sh
  else
    echo "Skipping $name: main branch does not exist."
  fi
'
