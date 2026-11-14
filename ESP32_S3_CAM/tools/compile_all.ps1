# compile_all.ps1 — Biên dịch firmware chính + 6 sketch kiểm thử bằng Arduino CLI (cùng core với Arduino IDE).
#
# Cài một lần (Arduino CLI: https://arduino.github.io/arduino-cli/latest/installation/):
#   arduino-cli config init
#   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
#   arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Xem tên tùy chọn FQBN của core đang cài:  arduino-cli board details -b esp32:esp32:esp32s3
#
# Chạy (từ thư mục project):  powershell -ExecutionPolicy Bypass -File ESP32_S3_CAM\tools\compile_all.ps1
param(
  [string]$Fqbn = "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,CPUFreq=240",
  [string]$Python = ".venv\Scripts\python.exe"
)

$fw = Split-Path -Parent $PSScriptRoot
if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
  Write-Host "arduino-cli khong co tren PATH - xem huong dan o dau file." -ForegroundColor Red
  exit 2
}
if (Test-Path $Python) {
  & $Python (Join-Path $PSScriptRoot "sync_project.py") --check
  if ($LASTEXITCODE -ne 0) { Write-Host "examples lech voi firmware chinh: chay tools\sync_project.py" -ForegroundColor Red; exit 1 }
}

arduino-cli version
arduino-cli core list
$sketches = @(Join-Path $fw "Potato_Classifier_ESP32S3") + (Get-ChildItem (Join-Path $fw "examples") -Directory | ForEach-Object { $_.FullName })
$results = @()
foreach ($s in $sketches) {
  Write-Host "`n=== $s ===" -ForegroundColor Cyan
  arduino-cli compile --fqbn $Fqbn --warnings default $s
  $results += [pscustomobject]@{ Sketch = (Split-Path -Leaf $s); Result = $(if ($LASTEXITCODE -eq 0) { "OK" } else { "FAILED" }) }
}
$results | Format-Table -AutoSize
if ($results.Result -contains "FAILED") { exit 1 }
