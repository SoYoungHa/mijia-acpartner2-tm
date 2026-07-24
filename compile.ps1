# MijiaPowerPlugin 编译脚本 (x64 Release)
# 使用: powershell -ExecutionPolicy Bypass -File compile.ps1

Write-Host ""
Write-Host "========================================"
Write-Host " MijiaPowerPlugin 编译开始 (x64)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

# 查找 MSBuild.exe
$msbuildPaths = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuildExe = $null
foreach ($path in $msbuildPaths) {
    if (Test-Path $path) {
        $msbuildExe = $path
        Write-Host "[√] 找到 MSBuild: $path" -ForegroundColor Green
        break
    }
}

if ($null -eq $msbuildExe) {
    Write-Host "[×] 错误: 未找到 MSBuild.exe" -ForegroundColor Red
    Write-Host "请安装 Visual Studio 2022 或 Visual Studio Build Tools"
    Read-Host "按任意键继续"
    exit 1
}

Write-Host ""
Write-Host "[*] 配置: Release | x64" -ForegroundColor Yellow
Write-Host "[*] 开始编译..." -ForegroundColor Yellow
Write-Host ""

# 执行编译
& $msbuildExe "MijiaPowerPlugin.sln" /p:Configuration=Release /p:Platform=x64 /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host " [√] 编译成功！" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    
    $dllPath = "bin\Release\x64\MijiaPower.dll"
    if (Test-Path $dllPath) {
        Write-Host "DLL 位置: $dllPath" -ForegroundColor Green
        Write-Host "文件大小: $((Get-Item $dllPath).Length) 字节" -ForegroundColor Green
    } else {
        Write-Host "[!] 未找到 DLL，搜索中..." -ForegroundColor Yellow
        Get-ChildItem -Recurse -Filter "MijiaPower.dll" 2>$null | ForEach-Object {
            Write-Host "找到: $($_.FullName)" -ForegroundColor Green
        }
    }
    
    Write-Host ""
    Write-Host "下一步: 关闭 TrafficMonitor，复制 DLL 到插件目录" -ForegroundColor Cyan
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host " [×] 编译失败！错误代码: $LASTEXITCODE" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
}

Read-Host "按任意键继续"
