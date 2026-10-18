@echo off

if "%~1"=="" (
  git fetch origin main
  git checkout main
  git pull origin main
) else (
  if "%~1"=="reset" (
    git checkout main
    git fetch origin
    git reset --hard origin/main
    powershell -command "git fetch --prune; git branch -vv | Select-String ': gone]' | ForEach-Object { git branch -D $_.ToString().Trim().Split()[0] }"
  ) else (
    git fetch origin %1
    git checkout -b %1
    git pull origin %1
  )
)
