<#
.SYNOPSIS
  One-click installer for AviUtl2 ProxyLayer.

.DESCRIPTION
  Installs ProxyLayer.aux2 into the AviUtl2 data folder and, optionally,
  replaces luaJIT.dll with the bundled same-name forwarding proxy:
     original luaJIT.dll  ->  backup  ->  renamed to luaJIT_real.dll
     bundled proxy DLL    ->  copied to luaJIT.dll
  PE forwarders inside the proxy resolve every LuaJIT export against
  luaJIT_real.dll, which is why the rename has to happen first.

  Idempotent. All overwritten files get a .bak-<timestamp> copy.
  -Uninstall restores the original DLL and moves the plugin aside
  (nothing is ever deleted, so every step stays recoverable).

.PARAMETER ExePath
  Explicit path to aviutl2.exe (skips auto-detection).
.PARAMETER Uninstall
  Restore original luaJIT.dll and remove ProxyLayer.aux2.
.PARAMETER SkipLuaProxy
  Install only the main plugin; never touch luaJIT.dll.
.PARAMETER NoPause
  Do not print the "press any key" hint (used by automation).
#>
param(
    [string]$ExePath = "",
    [switch]$Uninstall,
    [switch]$SkipLuaProxy,
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

function Step($m) { Write-Host "[*] $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[!]  $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[X] $m" -ForegroundColor Red; exit 1 }
function Sha($p)  { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash }

function Resolve-HostExe {
    if ($ExePath) {
        if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
            Die "指定的 aviutl2.exe 不存在: $ExePath"
        }
        return (Resolve-Path -LiteralPath $ExePath).Path
    }
    # 1) a running AviUtl2 is the most reliable source of truth
    $proc = Get-Process aviutl2 -ErrorAction SilentlyContinue |
            Where-Object { $_.Path } | Select-Object -First 1
    if ($proc) { return $proc.Path }

    # 2) right next to this installer, or one level up
    $near = @((Join-Path $here 'aviutl2.exe'),
              (Join-Path (Split-Path $here -Parent) 'aviutl2.exe'))
    foreach ($c in $near) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return (Resolve-Path -LiteralPath $c).Path }
    }

    # 3) common install locations
    $cands = @()
    if ($env:ProgramFiles) { $cands += (Join-Path $env:ProgramFiles 'AviUtl2\aviutl2.exe') }
    if (${env:ProgramFiles(x86)}) { $cands += (Join-Path ${env:ProgramFiles(x86)} 'AviUtl2\aviutl2.exe') }
    if ($env:LOCALAPPDATA) { $cands += (Join-Path $env:LOCALAPPDATA 'Programs\AviUtl2\aviutl2.exe') }
    foreach ($c in $cands) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
    }

    # 4) shallow scan of fixed drives (depth 3 under common roots only)
    foreach ($drive in (Get-PSDrive -PSProvider FileSystem -ErrorAction SilentlyContinue)) {
        $rootPath = $drive.Root
        if (-not $rootPath) { continue }
        $bases = @($rootPath)
        foreach ($sub in @('Program Files', 'Program Files (x86)', 'Apps', 'Games', 'Tools', 'AviUtl2')) {
            $bases += (Join-Path $rootPath $sub)
        }
        foreach ($base in $bases) {
            if (-not (Test-Path -LiteralPath $base -PathType Container)) { continue }
            try {
                $hit = Get-ChildItem -LiteralPath $base -Filter 'aviutl2.exe' -Recurse -File `
                        -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($hit) { return $hit.FullName }
            } catch { }
        }
    }

    # 5) last resort: ask the user (only when auto-detection failed)
    $manual = Read-Host '未能自动找到 aviutl2.exe，请输入完整路径（直接回车放弃）'
    if ($manual -and (Test-Path -LiteralPath $manual -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $manual).Path
    }
    return $null
}

# ---------------------------------------------------------------------------
$exe = Resolve-HostExe
if (-not $exe) { Die '找不到 aviutl2.exe。请用 -ExePath "D:\path\to\aviutl2.exe" 重新运行。' }
$exeDir = Split-Path $exe -Parent
Step "宿主: $exe"

# NEVER create a `data` folder next to the exe: AviUtl2 switches to portable
# mode the moment it exists, which silently resets the whole environment.
$portable = Join-Path $exeDir 'data'
if (Test-Path -LiteralPath $portable -PathType Container) {
    $dataRoot = $portable
    Step "数据目录（便携模式）: $dataRoot"
} else {
    $dataRoot = Join-Path $env:ProgramData 'aviutl2'
    Step "数据目录: $dataRoot"
}
if (-not (Test-Path -LiteralPath $dataRoot -PathType Container)) {
    Die "数据目录不存在: $dataRoot（请先启动一次 AviUtl2）"
}
$pluginDir = Join-Path $dataRoot 'Plugin'
$dstPlugin = Join-Path $pluginDir 'ProxyLayer.aux2'
$srcPlugin = Join-Path $here 'Plugin\ProxyLayer.aux2'

$proxySrc = Join-Path $here 'LuaProxy\luaJIT.dll'
$origDll  = Join-Path $exeDir 'luaJIT.dll'
$realDll  = Join-Path $exeDir 'luaJIT_real.dll'

if ($Uninstall) {
    Step '开始卸载 / 还原 ...'
    # Never DELETE anything: rename aside so every step stays recoverable
    # (and so the script also works under restrictive managed environments).
    if (Test-Path -LiteralPath $dstPlugin -PathType Leaf) {
        Rename-Item -LiteralPath $dstPlugin -NewName "ProxyLayer.aux2.removed-$stamp" -Force
        Ok "主插件已移开（保留为 ProxyLayer.aux2.removed-$stamp）"
    }
    if (Test-Path -LiteralPath $realDll -PathType Leaf) {
        if (Test-Path -LiteralPath $origDll -PathType Leaf) {
            Rename-Item -LiteralPath $origDll -NewName "luaJIT.dll.proxy-removed-$stamp" -Force
            Ok "代理 DLL 已移开（保留为 luaJIT.dll.proxy-removed-$stamp）"
        }
        Rename-Item -LiteralPath $realDll -NewName 'luaJIT.dll' -Force
        Ok '原 luaJIT.dll 已还原'
    } else {
        $bak = Get-ChildItem -LiteralPath $exeDir -Filter 'luaJIT.dll.bak-*' -File -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($bak) {
            Copy-Item -LiteralPath $bak.FullName -Destination $origDll -Force
            Ok "已从备份还原 luaJIT.dll: $($bak.Name)"
        } else {
            Warn '没有找到 luaJIT_real.dll 或备份，跳过 DLL 还原'
        }
    }
    Ok '卸载完成，请重启 AviUtl2。'
    return
}

# ---- install main plugin --------------------------------------------------
if (-not (Test-Path -LiteralPath $srcPlugin -PathType Leaf)) {
    Die "安装包缺少 Plugin\ProxyLayer.aux2"
}
if (-not (Test-Path -LiteralPath $pluginDir -PathType Container)) {
    New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
}
if ((Test-Path -LiteralPath $dstPlugin -PathType Leaf) -and
    ((Sha $dstPlugin) -eq (Sha $srcPlugin))) {
    Ok '主插件已是本包版本，跳过（幂等）'
} else {
    if (Test-Path -LiteralPath $dstPlugin -PathType Leaf) {
        Copy-Item -LiteralPath $dstPlugin -Destination "$dstPlugin.bak-$stamp" -Force
        Ok "已备份旧插件: ProxyLayer.aux2.bak-$stamp"
    }
    Copy-Item -LiteralPath $srcPlugin -Destination $dstPlugin -Force
    Ok "主插件已安装 -> $dstPlugin"
}

# ---- install optional luaJIT proxy ---------------------------------------
if ($SkipLuaProxy) {
    Warn '按 -SkipLuaProxy 跳过 luaJIT 代理 DLL'
} elseif (-not (Test-Path -LiteralPath $proxySrc -PathType Leaf)) {
    Warn '安装包缺少 LuaProxy\luaJIT.dll，跳过（主插件不受影响）'
} elseif (-not (Test-Path -LiteralPath $origDll -PathType Leaf)) {
    Warn "宿主目录没有 luaJIT.dll: $exeDir"
    Warn '跳过代理安装（主插件仍可正常使用）'
} else {
    $proxyHash = Sha $proxySrc
    if ((Sha $origDll) -eq $proxyHash) {
        Ok '代理 DLL 已是本包版本，跳过（幂等）'
    } else {
        # Windows lets us RENAME a loaded DLL even while the host is running.
        # Renaming first frees the luaJIT.dll path, so the copy never hits a
        # sharing violation. The host keeps its old in-memory image until restart.
        Copy-Item -LiteralPath $origDll -Destination "$origDll.bak-$stamp" -Force
        Ok "已备份原 DLL: luaJIT.dll.bak-$stamp"
        if (-not (Test-Path -LiteralPath $realDll -PathType Leaf)) {
            Rename-Item -LiteralPath $origDll -NewName 'luaJIT_real.dll' -Force
            Ok '原 luaJIT.dll 已改名为 luaJIT_real.dll'
        } else {
            Copy-Item -LiteralPath $realDll -Destination "$realDll.bak-$stamp" -Force
            Ok 'luaJIT_real.dll 已存在，仅覆盖 luaJIT.dll'
        }
        Copy-Item -LiteralPath $proxySrc -Destination $origDll -Force
        Ok "转发代理已就位 -> $origDll"
        Ok 'PE forwarders 会解析到同目录的 luaJIT_real.dll'
    }
}

# ---- summary --------------------------------------------------------------
$running = Get-Process aviutl2 -ErrorAction SilentlyContinue
if ($running) {
    Warn 'AviUtl2 正在运行：文件已替换，但必须重启宿主才会加载新 DLL。'
} else {
    Ok '安装完成，启动 AviUtl2 即可使用。'
}
Write-Host ''
Write-Host '  下一步：编辑菜单 -> 代理图层 -> 整个工程自动代理 (开/关)' -ForegroundColor White
Write-Host '  卸载：双击 uninstall.bat（会还原原 luaJIT.dll）' -ForegroundColor White
Write-Host ''
if (-not $NoPause -and $Host.Name -eq 'ConsoleHost') {
    Write-Host '按任意键关闭...' -ForegroundColor DarkGray
    try { $null = $Host.UI.RawUI.ReadKey('NoEcho,IncludeKeyDown') } catch { }
}
