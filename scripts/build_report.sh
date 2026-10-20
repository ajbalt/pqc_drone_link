#!/usr/bin/env bash
# Build a LaTeX report from docs/report with the MiKTeX engine installed on the Windows side
# (WSL2), because the Linux side has no LaTeX. The sources are copied to a Windows temp folder
# (cmd.exe cannot use a \\wsl.localhost path as its working directory), built there with
# latexmk, and the PDF and the log are copied back next to the source.
#
# Usage: build_report.sh [report-name] [--png]      default report: progress_report_2
#   --png  also render every page to PNG (scripts/render_pdf.ps1) in the Windows build folder,
#          under png/, to look at the diagrams; the folder is printed.
#   Needs MiKTeX with latexmk; missing packages are installed on the fly (--enable-installer).
# Exit status 0 if a PDF was produced without LaTeX errors; 1 otherwise (the log is kept).
set -eu
NAME=progress_report_2; PNG=no
for a in "$@"; do case $a in --png) PNG=yes ;; *) NAME=$a ;; esac; done
here=$(cd "$(dirname "$0")" && pwd); SRC="$here/../docs/report"
[ -f "$SRC/$NAME.tex" ] || { echo "no $SRC/$NAME.tex" >&2; exit 2; }
WIN_ROOT=$(cmd.exe /c "echo %LOCALAPPDATA%" 2>/dev/null | tr -d '\r')
[ -n "$WIN_ROOT" ] || { echo "cannot find the Windows temp folder (is this WSL?)" >&2; exit 2; }
WIN_DIR="$WIN_ROOT\\Temp\\pqc_report_build"
W=$(wslpath -u "$WIN_DIR")
rm -rf "$W" && mkdir -p "$W"
cp "$SRC/$NAME.tex" "$W/"
for d in data data2; do [ -d "$SRC/$d" ] && cp -r "$SRC/$d" "$W/"; done
printf '@echo off\r\ncd /d %s\r\nlatexmk -pdf -interaction=nonstopmode -file-line-error -pdflatex="pdflatex -interaction=nonstopmode -file-line-error --enable-installer %%%%O %%%%S" %s.tex > build.log 2>&1\r\n' \
       "$WIN_DIR" "$NAME" > "$W/build.bat"
cmd.exe /c "$WIN_DIR\\build.bat" > /dev/null 2>&1 || true
cp "$W/build.log" "$SRC/$NAME.build.log" 2>/dev/null || true
[ -f "$W/$NAME.log" ] && cp "$W/$NAME.log" "$SRC/$NAME.latex.log"
if [ -f "$W/$NAME.pdf" ] && ! grep -q "^$NAME.tex:[0-9]*:" "$W/$NAME.log"; then
  cp "$W/$NAME.pdf" "$SRC/$NAME.pdf"
  echo "built $SRC/$NAME.pdf ($(grep -c 'Overfull' "$W/$NAME.log" || true) overfull boxes)"
  if [ $PNG = yes ]; then
    cp "$here/render_pdf.ps1" "$W/"
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WIN_DIR\\render_pdf.ps1" \
        -Pdf "$WIN_DIR\\$NAME.pdf" -OutDir "$WIN_DIR\\png" | tail -1
    echo "pages as PNG in $W/png"
  fi
else
  echo "LaTeX errors; first ones:" >&2
  grep -n "^$NAME.tex:[0-9]*:" "$W/$NAME.log" | head -5 >&2
  exit 1
fi
