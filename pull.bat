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
  ) else (
    git fetch origin %1
    git checkout -b %1
    git pull origin %1
  )
)
