<#
.SYNOPSIS
  Lap monorepo ToolEngineer tu 4 nguon (app+server, firmware RapidPlus, firmware Reader,
  scaffold Production), GIU LICH SU bang git subtree. Chay MOT lan, cuc bo, KHONG push.

.DESCRIPTION
  Phuong an: docs/plan/monorepo-mot-he-thong.md (P0, buoc 3). Script nay:
    0. Tien kiem (repo dich 0 commit, repo nguon A/R sach, co git-filter-repo).
    1. Commit goc: README.md + .gitignore + .gitattributes.
    2. Repo A (app+server)  -> subtree add vao _import/app roi `git mv` ra dung cho:
         server/  legacy/{sheet,server-cf}  .claude/ .agents/ .codex/  system/  tools/  docs/plan/...
         phan con lai -> apps/fbt_rapid/.  `git rm --cached server/OTA/firmware.bin`.
    3. Repo B (firmware RapidPlus): clone --mirror vao scratch -> git-filter-repo TAY src/secrets.h
       (nhanh dang import van track file token that) -> kiem khong con token trong object DB
       -> subtree add vao firmware/rapidplus tu nhanh -FwBranch -> tag fw/rapidplus/<ver> va
       fw/rapidplus-a/<ver> (doc tu src/define.h) -> moi nhanh cu giu duoi refs/archive/fbt-dxd/<ten>,
       nhanh nao co commit rieng (khong nam trong nhanh import) duoc them tag archive/fbt-dxd/<ten>.
    4. Repo R (firmware Reader): subtree add vao firmware/reader, tag fw/reader/<ver>.
    5. Repo C (Production, 0 commit): robocopy sach (bo .git .pio .vscode) -> commit import.
  Repo nguon CHI DOC (tru viec `git fetch` tu chung). Xoa noi dung repo dich + chay lai = lam lai tu dau.

.PARAMETER DryRun        In moi lenh, khong chay gi (khong tao file, khong commit).
.PARAMETER Force         Cho phep chay khi repo dich da co commit (binh thuong tu choi).
.PARAMETER SkipFilterRepo  Dung lai mirror da tay san trong scratch (khong clone/filter lai).
.PARAMETER FwBranch      Nhanh firmware RapidPlus de import. Mac dinh = nhanh hien tai cua repo B.

.EXAMPLE
  & tools\monorepo\assemble.ps1 -DryRun
  & tools\monorepo\assemble.ps1
#>
[CmdletBinding()]
param(
  [switch]$DryRun,
  [switch]$Force,
  [switch]$SkipFilterRepo,
  [string]$FwBranch = "",
  [string]$Mono    = "C:\Users\ADM\Documents\01.Job\JOWO001-FORTE BIOTECH\00. Tool\06. ToolEngineer",
  [string]$SrcA    = "C:\Users\ADM\Documents\01.Job\JOWO001-FORTE BIOTECH\00. Tool\03. FBT-ToolRapidPlus",
  [string]$SrcB    = "C:\Users\ADM\Documents\01.Job\JOWO001-FORTE BIOTECH\04.RapidPlus\03.Firmware\FBT-DXD",
  [string]$SrcC    = "C:\Users\ADM\Documents\01.Job\JOWO001-FORTE BIOTECH\04.RapidPlus\03.Firmware\FBT-RapidPlus-Production",
  [string]$SrcR    = "C:\Users\ADM\Documents\01.Job\JOWO001-FORTE BIOTECH\02.RapidReader\PRV-Reader\reader",
  [string]$Scratch = "$env:LOCALAPPDATA\fbt-monorepo-scratch",
  [string]$Python  = "python"
)

$ErrorActionPreference = "Stop"
$script:StepNo = 0

function Say($msg, $color = "Cyan") { Write-Host $msg -ForegroundColor $color }
function Step($title) { $script:StepNo++; Write-Host ""; Write-Host ("=== [{0}] {1}" -f $script:StepNo, $title) -ForegroundColor Yellow }

# Chay git; in lenh; DryRun thi chi in. Loi -> throw.
# Ham THUONG (khong param block): moi tham so nam trong $args, ke ca -A/-a/-C/-q — neu khai bao
# param thi PowerShell bind `-A` vao tham so ten `$a` (khop tien to, khong phan biet hoa/thuong).
function G {
  $a = @($args)
  Write-Host ("  git " + ($a -join " ")) -ForegroundColor DarkGray
  if ($script:DryRun) { return }
  & git @a
  if ($LASTEXITCODE -ne 0) { throw ("git that bai (exit {0}): git {1}" -f $LASTEXITCODE, ($a -join " ")) }
}
# git tra ket qua (khong in), dung cho cau hoi; DryRun van chay vi chi doc.
function GQ {
  $a = @($args)
  # PS 5.1: stderr cua lenh native bi redirect + ErrorActionPreference=Stop -> nem loi. Ha xuong Continue trong scope nay.
  $ErrorActionPreference = "Continue"
  $out = & git @a 2>$null
  return $out
}
function MvIfExists($repo, $from, $to) {
  if ($DryRun) { Write-Host ("  git -C <mono> mv {0} {1}   (neu ton tai)" -f $from, $to) -ForegroundColor DarkGray; return }
  if (Test-Path (Join-Path $repo $from)) {
    $parent = Split-Path (Join-Path $repo $to) -Parent
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force $parent | Out-Null }
    G -C $repo mv $from $to
  } else { Write-Host ("  (bo qua, khong co: {0})" -f $from) -ForegroundColor DarkGray }
}
function WriteFile($path, $content) {
  Write-Host ("  ghi {0}" -f $path) -ForegroundColor DarkGray
  if ($DryRun) { return }
  $dir = Split-Path $path -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
  [IO.File]::WriteAllText($path, $content, (New-Object Text.UTF8Encoding($false)))
}
function VersionFrom($file, $pattern) {
  if (-not (Test-Path $file)) { return $null }
  $m = [regex]::Match((Get-Content $file -Raw), $pattern)
  if ($m.Success) { return $m.Groups[1].Value } else { return $null }
}

# ------------------------------------------------------------------------------------------
Step "Tien kiem"
foreach ($p in @($Mono, $SrcA, $SrcB, $SrcC, $SrcR)) {
  if (-not (Test-Path $p)) { throw "Khong thay thu muc: $p" }
}
$monoHead = GQ -C $Mono rev-parse --verify HEAD
if ($monoHead -and -not $Force) { throw "Repo dich da co commit ($monoHead). Xoa noi dung (giu .git) hoac them -Force." }
# Chay that: A va R phai sach (subtree lay COMMIT, thay doi chua commit se bi bo roi). DryRun: chi canh bao.
$dirtyA = GQ -C $SrcA status --porcelain
if ($dirtyA) { $m = "Repo A chua sach (commit truoc, khong thi thay doi KHONG vao monorepo):`n$($dirtyA -join "`n")"; if ($DryRun) { Say $m "Yellow" } else { throw $m } }
$dirtyR = GQ -C $SrcR status --porcelain
if ($dirtyR) { $m = "Repo R (reader) chua sach:`n$($dirtyR -join "`n")"; if ($DryRun) { Say $m "Yellow" } else { throw $m } }
$dirtyB = GQ -C $SrcB status --porcelain
if ($dirtyB) { Say "CANH BAO: repo B co thay doi chua commit - se KHONG vao monorepo (subtree lay commit):`n$($dirtyB -join "`n")" "Yellow" }
if (-not $FwBranch) { $FwBranch = (GQ -C $SrcB branch --show-current) }
if (-not $FwBranch) { throw "Khong xac dinh duoc nhanh firmware (repo B dang detached?). Truyen -FwBranch." }
$fwSha = GQ -C $SrcB rev-parse --short $FwBranch
if (-not $fwSha) { throw "Nhanh $FwBranch khong ton tai trong repo B" }
$aSha = GQ -C $SrcA rev-parse --short main
$rSha = GQ -C $SrcR rev-parse --short main
$mirror = Join-Path $Scratch "fbt-dxd-clean.git"
if (-not $SkipFilterRepo) {
  & { $ErrorActionPreference = "Continue"; & $Python -m git_filter_repo --version 2>$null | Out-Null }
  if ($LASTEXITCODE -ne 0) { throw "Thieu git-filter-repo: $Python -m pip install --user git-filter-repo" }
  if (Test-Path $mirror) { throw "Mirror da ton tai: $mirror. Xoa no, hoac -SkipFilterRepo de dung lai." }
} elseif (-not (Test-Path $mirror)) { throw "-SkipFilterRepo nhung khong co mirror: $mirror" }
$subtreeHelp = GQ subtree   # usage cua git-subtree (exit 129) -> co lenh; khong redirect 2>&1 (PS 5.1 in RemoteException)
if (-not $subtreeHelp -and $LASTEXITCODE -ne 129) { throw "git subtree khong co san (Git for Windows thuong kem)" }
Say ("Dich:  {0}" -f $Mono)
Say ("A app+server: {0}@{1}" -f $SrcA, $aSha)
Say ("B firmware:   {0}@{1} (nhanh {2})" -f $SrcB, $fwSha, $FwBranch)
Say ("R reader:     {0}@{1}" -f $SrcR, $rSha)
Say ("C production: {0} (0 commit, copy)" -f $SrcC)
if ($DryRun) { Say "DRY RUN - khong thay doi gi" "Magenta" }
G -C $Mono config core.longpaths true

# ------------------------------------------------------------------------------------------
Step "Commit goc (README, .gitignore, .gitattributes)"
$readme = @"
# ToolEngineer - he thong san pham Forte Biotech (monorepo)

Mot repo cho ca ba tang: **server** (Engineer Server, FastAPI) - **client** (app FBT_RAPID, Flutter
desktop + web) - **thiet bi** (firmware ESP32 tung san pham). Nguon su that ve san pham:
``system/products.yaml``. Ban do va quy tac: ``CLAUDE.md``; phuong an gom: ``docs/plan/monorepo-mot-he-thong.md``.

| Thu muc | Noi dung |
|---|---|
| ``system/`` | registry san pham + hop dong du lieu (JSON Schema) |
| ``server/`` | Engineer Server (FastAPI + Postgres) - deploy len box ``hub.fortebio.tech`` |
| ``apps/fbt_rapid/`` | App FBT_RAPID (Flutter) - desktop Windows + web ``/app/`` |
| ``firmware/rapidplus/`` | Firmware Forte Rapid+ (PlatformIO, ESP32) |
| ``firmware/rapidplus-prod/`` | Firmware Rapid+ viet lai theo IEC 62304 (dev) |
| ``firmware/reader/`` | Firmware Forte Rapid Reader |
| ``legacy/`` | Apps Script (getData.js CON SONG cho fleet cu), port Cloudflare Workers chua deploy |
| ``tools/`` | registry_check.py, script lap monorepo |
| ``docs/`` | tai lieu xuyen phan (plan, history); tai lieu tung phan nam trong thu muc cua no |
"@
$gitignore = @"
# --- bi mat: KHONG BAO GIO commit ---
**/secrets.h
!**/secrets.example.h
*.env
!*.env.example
# --- build / cache ---
.pio/
**/__pycache__/
*.pyc
.pytest_cache/
venv/
.venv/
node_modules/
server/OTA/
# --- anh chup / tam ---
_smoke.png
_web.png
*.zip
*.rar
*.tmp
Thumbs.db
.DS_Store
.idea/
"@
$gitattributes = @"
# Chi danh dau BINARY. KHONG dat 'text=auto' o day: se renormalize hang loat blob da import.
*.bin -text
*.png -text
*.jpg -text
*.ico -text
*.ttf -text
*.otf -text
*.docx -text
*.pdf -text
*.gz -text
"@
WriteFile (Join-Path $Mono "README.md") $readme
WriteFile (Join-Path $Mono ".gitignore") $gitignore
WriteFile (Join-Path $Mono ".gitattributes") $gitattributes
G -C $Mono add -A
G -C $Mono commit -q -m "chore: khoi tao monorepo ToolEngineer (Forte Biotech)"

# ------------------------------------------------------------------------------------------
Step "Repo A (app + server) -> subtree add _import/app -> git mv ra dung cho"
G -C $Mono remote add src-app $SrcA
G -C $Mono fetch -q src-app --no-tags
G -C $Mono subtree add --prefix=_import/app src-app/main -m ("import(app+server): wuanpham/FBT-ToolRapidPlus@{0} (git subtree, giu lich su)" -f $aSha)
MvIfExists $Mono "_import/app/server"    "server"
MvIfExists $Mono "_import/app/server-cf" "legacy/server-cf"
MvIfExists $Mono "_import/app/sheet"     "legacy/sheet"
MvIfExists $Mono "_import/app/.claude"   ".claude"
MvIfExists $Mono "_import/app/.agents"   ".agents"
MvIfExists $Mono "_import/app/.codex"    ".codex"
MvIfExists $Mono "_import/app/system"    "system"
MvIfExists $Mono "_import/app/tools"     "tools"
MvIfExists $Mono "_import/app/docs/plan/monorepo-mot-he-thong.md" "docs/plan/monorepo-mot-he-thong.md"
MvIfExists $Mono "_import/app"           "apps/fbt_rapid"
if (-not $DryRun -and (GQ -C $Mono ls-files --error-unmatch server/OTA/firmware.bin)) {
  G -C $Mono rm -q --cached server/OTA/firmware.bin
}
G -C $Mono commit -q -m "restructure: server/ legacy/ system/ tools/ .claude/ ra goc; app -> apps/fbt_rapid"
G -C $Mono remote remove src-app

# ------------------------------------------------------------------------------------------
Step "Repo B (firmware RapidPlus): tay src/secrets.h khoi lich su"
$tokFile = Join-Path $Scratch "tokens.txt"
$repFile = Join-Path $Scratch "tokens-replace.txt"
if (-not (Test-Path $Scratch)) { if (-not $DryRun) { New-Item -ItemType Directory -Force $Scratch | Out-Null } }
# Trich gia tri token that tu secrets.h dang co trong cay lam viec cua B (KHONG in ra man hinh).
$secretsSrc = Join-Path $SrcB "src\secrets.h"
$tokens = @()
if (Test-Path $secretsSrc) {
  Get-Content $secretsSrc | ForEach-Object {
    $m = [regex]::Match($_, '#define\s+(SECRET_\w*(TOKEN|KEY)\w*)\s+"([^"]{12,})"')
    if ($m.Success) { $tokens += $m.Groups[3].Value }
  }
}
Say ("  tim thay {0} gia tri token trong src/secrets.h (khong in)" -f $tokens.Count)
if (-not $DryRun -and $tokens.Count -gt 0) {
  [IO.File]::WriteAllLines($tokFile, $tokens, (New-Object Text.ASCIIEncoding))
  [IO.File]::WriteAllLines($repFile, ($tokens | ForEach-Object { "$_==>***REMOVED***" }), (New-Object Text.ASCIIEncoding))
}
if (-not $SkipFilterRepo) {
  G clone -q --mirror $SrcB $mirror
  Write-Host ("  {0} -m git_filter_repo --invert-paths --path src/secrets.h   (trong {1})" -f $Python, $mirror) -ForegroundColor DarkGray
  if (-not $DryRun) {
    Push-Location $mirror
    try {
      $ErrorActionPreference = "Continue"   # git grep/log ghi stderr + redirect -> khong duoc nem; kiem exit code tay
      & $Python -m git_filter_repo --invert-paths --path src/secrets.h
      if ($LASTEXITCODE -ne 0) { throw "git-filter-repo that bai" }
      $left = & git log --all --oneline -- src/secrets.h
      if ($left) { throw "Van con commit cham src/secrets.h sau filter-repo:`n$($left -join "`n")" }
      if ($tokens.Count -gt 0) {
        $revs = & git rev-list --all
        $hits = & git grep -I -l -F -f $tokFile @revs 2>$null
        $msgHits = @()
        foreach ($t in $tokens) { $mh = & git log --all --format=%H -F --grep=$t; if ($mh) { $msgHits += $mh } }
        if ($hits -or $msgHits) {
          Say ("  token con xuat hien o {0} blob / {1} commit message -> replace-text/replace-message" -f @($hits).Count, @($msgHits).Count) "Yellow"
          & $Python -m git_filter_repo --replace-text $repFile --replace-message $repFile --force
          if ($LASTEXITCODE -ne 0) { throw "git-filter-repo --replace-text that bai" }
          $revs = & git rev-list --all
          $hits = & git grep -I -l -F -f $tokFile @revs 2>$null
          if ($hits) { throw "Van con token trong blob sau replace-text:`n$($hits -join "`n")" }
        }
        Say "  OK: khong con token trong object DB cua mirror" "Green"
      }
    } finally { Pop-Location; $ErrorActionPreference = "Stop" }
  }
}

Step "Repo B: subtree add firmware/rapidplus + tag + refs/archive"
G -C $Mono remote add src-fw $mirror
G -C $Mono fetch -q src-fw --no-tags "+refs/heads/*:refs/import/fbt-dxd/local/*" "+refs/remotes/origin/*:refs/import/fbt-dxd/gh/*"
$importRef = "refs/import/fbt-dxd/local/$FwBranch"
G -C $Mono subtree add --prefix=firmware/rapidplus $importRef -m ("import(firmware/rapidplus): wuanpham/FBT-DXD nhanh {0}@{1} (lich su da tay src/secrets.h, hash doi)" -f $FwBranch, $fwSha)
$defineH = Join-Path $Mono "firmware\rapidplus\src\define.h"
$verBase = VersionFrom $defineH '#else\s*\r?\n\s*#define\s+FIRMWARE_VERSION\s+"(v[^"]+)"'
$verA    = VersionFrom $defineH 'SHAPE_RULE_NEGATIVE\s*\r?\n\s*#define\s+FIRMWARE_VERSION\s+"(v[^"]+)"'
if ($verBase) { G -C $Mono tag -a "fw/rapidplus/$verBase" -m ("Firmware RapidPlus dang chay tren fleet (env esp32dev); nguon FBT-DXD {0}@{1}" -f $FwBranch, $fwSha) HEAD }
else { Say "  CANH BAO: khong doc duoc FIRMWARE_VERSION (nhanh #else) - khong tag fw/rapidplus" "Yellow" }
if ($verA) { G -C $Mono tag -a "fw/rapidplus-a/$verA" -m ("Firmware RapidPlus bien the A (env esp32dev_shape_neg); nguon FBT-DXD {0}@{1}" -f $FwBranch, $fwSha) HEAD }
else { Say "  CANH BAO: khong doc duoc FIRMWARE_VERSION (nhanh SHAPE_RULE_NEGATIVE) - khong tag fw/rapidplus-a" "Yellow" }
# Moi nhanh cu -> refs/archive/fbt-dxd/<ten> (gh truoc, local ghi de); nhanh co commit rieng -> them tag.
if (-not $DryRun) {
  $archived = 0; $tagged = @()
  $ghRefs = GQ -C $Mono for-each-ref --format="%(refname)" refs/import/fbt-dxd/gh/
  $lcRefs = GQ -C $Mono for-each-ref --format="%(refname)" refs/import/fbt-dxd/local/
  foreach ($r in @($ghRefs) + @($lcRefs)) {
    if (-not $r) { continue }
    $name = $r -replace '^refs/import/fbt-dxd/(gh|local)/', ''
    if ($name -eq "HEAD") { continue }
    G -C $Mono update-ref "refs/archive/fbt-dxd/$name" $r
    $archived++
    $uniq = [int](GQ -C $Mono rev-list --count "$importRef..$r")
    if ($uniq -gt 0 -and $name -ne $FwBranch) {
      $tag = "archive/fbt-dxd/$name"
      if (-not (GQ -C $Mono tag -l $tag)) {
        G -C $Mono tag -a $tag -m ("Nhanh cu FBT-DXD '{0}': {1} commit KHONG nam trong nhanh import {2}. Lay lai: git cherry-pick -x -Xsubtree=firmware/rapidplus <sha>" -f $name, $uniq, $FwBranch) $r
        $tagged += "$name($uniq)"
      }
    }
  }
  Say ("  refs/archive/fbt-dxd/*: {0} nhanh; tag archive cho nhanh co commit rieng: {1}" -f $archived, ($tagged -join ", "))
  foreach ($r in @($ghRefs) + @($lcRefs)) { if ($r) { & git -C $Mono update-ref -d $r } }
} else { Write-Host "  (DryRun) refs/import/fbt-dxd/* -> refs/archive/fbt-dxd/*, tag archive/fbt-dxd/<nhanh co commit rieng>" -ForegroundColor DarkGray }
G -C $Mono remote remove src-fw

# ------------------------------------------------------------------------------------------
Step "Repo R (firmware Reader): subtree add firmware/reader"
G -C $Mono remote add src-reader $SrcR
G -C $Mono fetch -q src-reader --no-tags
G -C $Mono subtree add --prefix=firmware/reader src-reader/main -m ("import(firmware/reader): fortebio/reader@{0} (git subtree, giu lich su)" -f $rSha)
$verR = VersionFrom (Join-Path $Mono "firmware\reader\src\define.h") 'FirmwareVer\s*=\s*\{?\s*"(v[^"]+)"'
if ($verR) { G -C $Mono tag -a "fw/reader/$verR" -m ("Firmware Reader dang ban; nguon fortebio/reader@{0}" -f $rSha) HEAD }
else { Say "  CANH BAO: khong doc duoc FirmwareVer cua reader - khong tag" "Yellow" }
G -C $Mono remote remove src-reader

# ------------------------------------------------------------------------------------------
Step "Repo C (Production, 0 commit): copy sach -> firmware/rapidplus-prod"
$dstC = Join-Path $Mono "firmware\rapidplus-prod"
Write-Host ("  robocopy `"{0}`" `"{1}`" /E /XD .git .pio .vscode /XF *.bin *.elf *.map" -f $SrcC, $dstC) -ForegroundColor DarkGray
if (-not $DryRun) {
  & robocopy $SrcC $dstC /E /XD .git .pio .vscode /XF *.bin *.elf *.map /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { throw "robocopy loi (exit $LASTEXITCODE)" }
  G -C $Mono add firmware/rapidplus-prod
  G -C $Mono commit -q -m "import(firmware/rapidplus-prod): scaffold IEC 62304 v0.1.0 (nguon FBT-RapidPlus-Production chua co commit, khong lich su)"
}

# ------------------------------------------------------------------------------------------
Step "Tong ket"
if (-not $DryRun) {
  & git -C $Mono log --oneline | Select-Object -First 8
  Write-Host "  ..."
  Say ("  tong commit: {0}" -f (& git -C $Mono rev-list --count HEAD))
  Say ("  tags: " + ((& git -C $Mono tag -l) -join ", "))
  Say ("  refs/archive: {0}" -f @(& git -C $Mono for-each-ref refs/archive/).Count)
  Say ("  KIEM: git -C <mono> log --all --oneline -- firmware/rapidplus/src/secrets.h  (phai rong)")
  if ($tokens.Count -gt 0) { Say ("  KIEM: git -C <mono> grep -I -l -F -f {0} `$(git rev-list --all)  (phai rong) roi XOA file do" -f $tokFile) "Yellow" }
  Say "  Tiep: commit fix(paths) -> docs: tach CLAUDE.md -> ci: -> chay bo kiem chung (docs/plan/monorepo-mot-he-thong.md)" "Green"
} else { Say "DRY RUN xong - khong thay doi gi." "Magenta" }
