<#
.SYNOPSIS
  Lap monorepo ToolEngineer tu 4 nguon (app+server, firmware RapidPlus, firmware Reader,
  scaffold Production), GIU LICH SU TUNG FILE. Chay cuc bo, KHONG push.

.DESCRIPTION
  Phuong an: docs/plan/monorepo-mot-he-thong.md (P0, buoc 3).

  CO CHE (doi tu `git subtree add` sang filter-repo + merge, 2026-09-15): `git subtree add` giu
  COMMIT nhung file trong cac commit cu van nam o path goc (src/define.h), nen
  `git log -- firmware/rapidplus/src/define.h` chi thay 1 commit (merge) - dung dieu phuong an
  hua la "lich su tung file con nguyen". Vi firmware B da bat buoc qua git-filter-repo (tay token),
  them `--to-subdirectory-filter <prefix>` cho CA BA nguon la mien phi: moi commit cu mang path da
  prefix -> `git log -- <file>`, `git blame`, cherry-pick tu nhanh archive deu chay thang.
  Danh doi: hash commit cua A/R trong monorepo KHAC repo goc (B thi da khac vi tay token).

  Cac buoc:
    0. Tien kiem (repo dich 0 commit hoac -Force, repo nguon A/R sach, co git-filter-repo,
       Python NHIN THAY thu muc scratch).
    1. Commit goc: README.md + .gitignore + .gitattributes.
    2. Repo A (app+server): mirror -> filter-repo (bo server/OTA/firmware.bin khoi lich su;
       moi thu vao apps/fbt_rapid/ roi doi cho: server/ legacy/{sheet,server-cf} .claude/ .agents/
       .codex/ system/ tools/ docs/plan/monorepo-*.md ra goc) -> merge --allow-unrelated-histories.
    3. Repo B (firmware RapidPlus): mirror -> filter-repo TAY src/secrets.h + prefix firmware/rapidplus/
       -> kiem khong con token trong object DB (con sot -> --replace-text) -> merge nhanh -FwBranch
       -> tag fw/rapidplus/<ver> va fw/rapidplus-a/<ver> (doc tu src/define.h) -> moi nhanh cu giu
       duoi refs/archive/fbt-dxd/<ten> (da prefix, cherry-pick thang), nhanh co commit rieng them tag
       archive/fbt-dxd/<ten>.
    4. Repo R (firmware Reader): mirror -> prefix firmware/reader/ -> merge, tag fw/reader/<ver>.
    5. Repo C (Production, 0 commit): robocopy sach (bo .git .pio .vscode) -> commit import.
  CHAY TIEP DUOC: buoc nao da co file moc trong repo dich (apps/fbt_rapid/pubspec.yaml,
  firmware/*/platformio.ini) thi bo qua (can -Force vi repo dich da co commit).
  Repo nguon CHI DOC (chi `git clone`/`fetch` tu chung). Lam lai tu dau: xoa toan bo repo dich
  (ke ca .git), `git init -b main` + `git remote add origin ...`, xoa thu muc scratch, chay lai.

.PARAMETER DryRun          In moi lenh, khong chay gi (khong tao file, khong commit).
.PARAMETER Force           Cho phep chay khi repo dich da co commit (= chay tiep cac buoc con thieu).
.PARAMETER SkipFilterRepo  Dung lai cac mirror da loc san trong scratch (khong clone/filter lai).
.PARAMETER FwBranch        Nhanh firmware RapidPlus de import. Mac dinh = nhanh hien tai cua repo B.
.PARAMETER Scratch         Thu muc tam cho mirror + tokens. MAC DINH %TEMP%\fbt-monorepo-scratch, KHONG
                           phai %LOCALAPPDATA%: Python 3.14 (Python Install Manager/Store) bi Windows AO HOA
                           AppData\Local -> thu muc git vua tao o do Python (va git-filter-repo) bao
                           "No such file or directory". %TEMP% thi PowerShell, Python, git deu thay.

.EXAMPLE
  & tools\monorepo\assemble.ps1 -DryRun
  & tools\monorepo\assemble.ps1
  & tools\monorepo\assemble.ps1 -Force        # chay tiep sau khi mot buoc hong
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
  [string]$Scratch = (Join-Path $env:TEMP "fbt-monorepo-scratch"),
  [string]$Python  = "python"
)

$ErrorActionPreference = "Stop"
$script:StepNo = 0
$tokens = @()

function Say($msg, $color = "Cyan") { Write-Host $msg -ForegroundColor $color }
function Step($title) { $script:StepNo++; Write-Host ""; Write-Host ("=== [{0}] {1}" -f $script:StepNo, $title) -ForegroundColor Yellow }

# Ham THUONG (khong param block): moi tham so nam trong $args, ke ca -A/-a/-C/-q - neu khai bao
# param thi PowerShell bind `-A` vao tham so ten `$a` (khop tien to, khong phan biet hoa/thuong).
function G {
  $a = @($args)
  Write-Host ("  git " + ($a -join " ")) -ForegroundColor DarkGray
  if ($script:DryRun) { return }
  # git hay ghi canh bao ra stderr (CRLF, progress). Neu NGUOI GOI redirect 2>&1 thi PS 5.1 bien no
  # thanh NativeCommandError va Stop se dung script giua chung -> chi tin exit code.
  $ErrorActionPreference = "Continue"
  & git @a
  if ($LASTEXITCODE -ne 0) { throw ("git that bai (exit {0}): git {1}" -f $LASTEXITCODE, ($a -join " ")) }
}
# git tra ket qua (khong in), dung cho cau hoi; DryRun van chay vi chi doc.
function GQ {
  $a = @($args)
  $ErrorActionPreference = "Continue"
  $out = & git @a 2>$null
  return $out
}
# Chay git-filter-repo trong mot mirror. $a = tham so filter-repo. Loi -> throw.
function FilterRepo($mirrorDir, [string[]]$a) {
  Write-Host ("  {0} -m git_filter_repo {1}   (trong {2})" -f $Python, ($a -join " "), $mirrorDir) -ForegroundColor DarkGray
  if ($script:DryRun) { return }
  Push-Location $mirrorDir
  try {
    $ErrorActionPreference = "Continue"
    & $Python -m git_filter_repo @a
    if ($LASTEXITCODE -ne 0) { throw ("git-filter-repo that bai: {0}" -f ($a -join " ")) }
  } finally { Pop-Location; $ErrorActionPreference = "Stop" }
}
# Clone mirror SACH cho filter-repo: --no-local (filter-repo tu choi clone hardlink), bo refs/stash
# (mirror mang ca stash cua nguon -> "has stashed changes"; stash khong phai lich su).
function MirrorClone($src, $dst) {
  G clone -q --mirror --no-local $src $dst
  if (-not $script:DryRun) { GQ -C $dst update-ref -d refs/stash | Out-Null }
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
if ($monoHead -and -not $Force) { throw "Repo dich da co commit ($monoHead). Xoa noi dung (giu .git) hoac them -Force de chay tiep." }
# Chay tiep tu buoc do: buoc nao da co file moc trong repo dich thi bo qua.
$haveA = Test-Path (Join-Path $Mono "apps\fbt_rapid\pubspec.yaml")
$haveB = Test-Path (Join-Path $Mono "firmware\rapidplus\platformio.ini")
$haveR = Test-Path (Join-Path $Mono "firmware\reader\platformio.ini")
$haveC = Test-Path (Join-Path $Mono "firmware\rapidplus-prod\platformio.ini")
if ($haveA) { Say "  (da co apps/fbt_rapid -> bo qua commit goc + import A)" "DarkGray" }
if ($haveB) { Say "  (da co firmware/rapidplus -> bo qua import B)" "DarkGray" }
if ($haveR) { Say "  (da co firmware/reader -> bo qua import R)" "DarkGray" }
if ($haveC) { Say "  (da co firmware/rapidplus-prod -> bo qua import C)" "DarkGray" }
# Chay that: A va R phai sach (import lay COMMIT, thay doi chua commit se bi bo roi). DryRun: chi canh bao.
$dirtyA = GQ -C $SrcA status --porcelain
if ($dirtyA -and -not $haveA) { $m = "Repo A chua sach (commit truoc, khong thi thay doi KHONG vao monorepo):`n$($dirtyA -join "`n")"; if ($DryRun) { Say $m "Yellow" } else { throw $m } }
$dirtyR = GQ -C $SrcR status --porcelain
if ($dirtyR -and -not $haveR) { $m = "Repo R (reader) chua sach:`n$($dirtyR -join "`n")"; if ($DryRun) { Say $m "Yellow" } else { throw $m } }
$dirtyB = GQ -C $SrcB status --porcelain
if ($dirtyB -and -not $haveB) { Say "CANH BAO: repo B co thay doi chua commit - se KHONG vao monorepo (import lay commit):`n$($dirtyB -join "`n")" "Yellow" }
if (-not $FwBranch) { $FwBranch = (GQ -C $SrcB branch --show-current) }
if (-not $FwBranch) { throw "Khong xac dinh duoc nhanh firmware (repo B dang detached?). Truyen -FwBranch." }
$fwSha = GQ -C $SrcB rev-parse --short $FwBranch
if (-not $fwSha) { throw "Nhanh $FwBranch khong ton tai trong repo B" }
$aSha = GQ -C $SrcA rev-parse --short main
$rSha = GQ -C $SrcR rev-parse --short main
$mirA = Join-Path $Scratch "app-clean.git"
$mirB = Join-Path $Scratch "fbt-dxd-clean.git"
$mirR = Join-Path $Scratch "reader-clean.git"
$needFilter = (-not $haveA) -or (-not $haveB) -or (-not $haveR)
if ($needFilter) {
  if (-not $SkipFilterRepo) {
    & { $ErrorActionPreference = "Continue"; & $Python -m git_filter_repo --version 2>$null | Out-Null }
    if ($LASTEXITCODE -ne 0) { throw "Thieu git-filter-repo: $Python -m pip install --user git-filter-repo" }
    foreach ($m in @($mirA, $mirB, $mirR)) { if (Test-Path $m) { throw "Mirror da ton tai: $m. Xoa thu muc scratch, hoac -SkipFilterRepo de dung lai." } }
  }
  # Python phai NHIN THAY $Scratch (xem chu thich tham so -Scratch).
  if (-not $DryRun) {
    if (-not (Test-Path $Scratch)) { New-Item -ItemType Directory -Force $Scratch | Out-Null }
    & $Python -c "import os,sys; sys.exit(0 if os.path.isdir(sys.argv[1]) else 3)" $Scratch
    if ($LASTEXITCODE -ne 0) { throw "Python KHONG thay $Scratch (ao hoa AppData cua Store-Python). Truyen -Scratch toi thu muc khac (vd duoi %TEMP%)." }
  }
}
Say ("Dich:  {0}" -f $Mono)
Say ("A app+server: {0}@{1}" -f $SrcA, $aSha)
Say ("B firmware:   {0}@{1} (nhanh {2})" -f $SrcB, $fwSha, $FwBranch)
Say ("R reader:     {0}@{1}" -f $SrcR, $rSha)
Say ("C production: {0} (0 commit, copy)" -f $SrcC)
Say ("Scratch:      {0}" -f $Scratch)
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
if ($haveA) { Say "  bo qua (da co)" "DarkGray" } else {
  WriteFile (Join-Path $Mono "README.md") $readme
  WriteFile (Join-Path $Mono ".gitignore") $gitignore
  WriteFile (Join-Path $Mono ".gitattributes") $gitattributes
  G -C $Mono add -A
  G -C $Mono commit -q -m "chore: khoi tao monorepo ToolEngineer (Forte Biotech)"
}

# ------------------------------------------------------------------------------------------
Step "Repo A (app + server): filter-repo doi path -> merge"
if ($haveA) { Say "  bo qua (da co)" "DarkGray" } else {
  if (-not $SkipFilterRepo) {
    MirrorClone $SrcA $mirA
    # Lan 1: bo blob .bin 2 MB khoi lich su + moi thu vao apps/fbt_rapid/.
    FilterRepo $mirA @("--invert-paths", "--path", "server/OTA/firmware.bin", "--to-subdirectory-filter", "apps/fbt_rapid")
    # Lan 2 (--force vi mirror khong con "tuoi"): doi cho cac thu muc xuyen phan ra goc.
    FilterRepo $mirA @("--force",
      "--path-rename", "apps/fbt_rapid/server/:server/",
      "--path-rename", "apps/fbt_rapid/server-cf/:legacy/server-cf/",
      "--path-rename", "apps/fbt_rapid/sheet/:legacy/sheet/",
      "--path-rename", "apps/fbt_rapid/.claude/:.claude/",
      "--path-rename", "apps/fbt_rapid/.agents/:.agents/",
      "--path-rename", "apps/fbt_rapid/.codex/:.codex/",
      "--path-rename", "apps/fbt_rapid/system/:system/",
      "--path-rename", "apps/fbt_rapid/tools/:tools/",
      "--path-rename", "apps/fbt_rapid/docs/plan/monorepo-mot-he-thong.md:docs/plan/monorepo-mot-he-thong.md")
  }
  G -C $Mono fetch -q $mirA --no-tags "+refs/heads/main:refs/import/app/main"
  G -C $Mono merge --allow-unrelated-histories --no-ff -q refs/import/app/main -m ("import(app+server): wuanpham/FBT-ToolRapidPlus@{0} - lich su day du, path da doi (filter-repo): app -> apps/fbt_rapid, server/ legacy/ system/ tools/ .claude/ o goc" -f $aSha)
  G -C $Mono update-ref -d refs/import/app/main
}

# ------------------------------------------------------------------------------------------
Step "Repo B (firmware RapidPlus): tay src/secrets.h + prefix firmware/rapidplus/"
$tokFile = Join-Path $Scratch "tokens.txt"
$repFile = Join-Path $Scratch "tokens-replace.txt"
if ($haveB) { Say "  bo qua (da co)" "DarkGray" } else {
  # Trich gia tri token that tu secrets.h dang co trong cay lam viec cua B (KHONG in ra man hinh).
  $secretsSrc = Join-Path $SrcB "src\secrets.h"
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
    MirrorClone $SrcB $mirB
    FilterRepo $mirB @("--invert-paths", "--path", "src/secrets.h", "--to-subdirectory-filter", "firmware/rapidplus")
    if (-not $DryRun) {
      Push-Location $mirB
      try {
        $ErrorActionPreference = "Continue"   # git grep/log ghi stderr + redirect -> khong duoc nem; kiem exit code tay
        $left = & git log --all --oneline -- firmware/rapidplus/src/secrets.h src/secrets.h
        if ($left) { throw "Van con commit cham secrets.h sau filter-repo:`n$($left -join "`n")" }
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
}

Step "Repo B: merge firmware/rapidplus + tag + refs/archive"
if ($haveB) { Say "  bo qua (da co)" "DarkGray" } else {
  G -C $Mono fetch -q $mirB --no-tags "+refs/heads/*:refs/import/fbt-dxd/local/*" "+refs/remotes/origin/*:refs/import/fbt-dxd/gh/*"
  $importRef = "refs/import/fbt-dxd/local/$FwBranch"
  G -C $Mono merge --allow-unrelated-histories --no-ff -q $importRef -m ("import(firmware/rapidplus): wuanpham/FBT-DXD nhanh {0}@{1} - lich su day du duoi firmware/rapidplus/, da tay src/secrets.h (hash doi)" -f $FwBranch, $fwSha)
  $defineH = Join-Path $Mono "firmware\rapidplus\src\define.h"
  $verBase = VersionFrom $defineH '#else\s*\r?\n\s*#define\s+FIRMWARE_VERSION\s+"(v[^"]+)"'
  $verA    = VersionFrom $defineH 'SHAPE_RULE_NEGATIVE\s*\r?\n\s*#define\s+FIRMWARE_VERSION\s+"(v[^"]+)"'
  if ($verBase) { G -C $Mono tag -a "fw/rapidplus/$verBase" -m ("FIRMWARE_VERSION (env esp32dev, nhanh #else) doc tu src/define.h tai commit import; nguon FBT-DXD {0}@{1}" -f $FwBranch, $fwSha) HEAD }
  else { Say "  CANH BAO: khong doc duoc FIRMWARE_VERSION (nhanh #else) - khong tag fw/rapidplus" "Yellow" }
  if ($verA) { G -C $Mono tag -a "fw/rapidplus-a/$verA" -m ("FIRMWARE_VERSION (env esp32dev_shape_neg, bien the A) doc tu src/define.h tai commit import; nguon FBT-DXD {0}@{1}" -f $FwBranch, $fwSha) HEAD }
  else { Say "  CANH BAO: khong doc duoc FIRMWARE_VERSION (nhanh SHAPE_RULE_NEGATIVE) - khong tag fw/rapidplus-a" "Yellow" }
  # Moi nhanh cu -> refs/archive/fbt-dxd/<ten> (gh truoc, local ghi de); nhanh co commit rieng -> them tag.
  # Path trong cac nhanh nay DA prefix firmware/rapidplus/ -> `git cherry-pick -x <sha>` chay thang.
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
          G -C $Mono tag -a $tag -m ("Nhanh cu FBT-DXD '{0}': {1} commit KHONG nam trong nhanh import {2}. Lay lai: git cherry-pick -x <sha> (path da prefix firmware/rapidplus/)" -f $name, $uniq, $FwBranch) $r
          $tagged += "$name($uniq)"
        }
      }
    }
    Say ("  refs/archive/fbt-dxd/*: {0} nhanh; tag archive cho nhanh co commit rieng: {1}" -f $archived, ($tagged -join ", "))
    foreach ($r in @($ghRefs) + @($lcRefs)) { if ($r) { & git -C $Mono update-ref -d $r } }
  } else { Write-Host "  (DryRun) refs/import/fbt-dxd/* -> refs/archive/fbt-dxd/*, tag archive/fbt-dxd/<nhanh co commit rieng>" -ForegroundColor DarkGray }
}

# ------------------------------------------------------------------------------------------
Step "Repo R (firmware Reader): prefix firmware/reader/ -> merge"
if ($haveR) { Say "  bo qua (da co)" "DarkGray" } else {
  if (-not $SkipFilterRepo) {
    MirrorClone $SrcR $mirR
    FilterRepo $mirR @("--to-subdirectory-filter", "firmware/reader")
  }
  G -C $Mono fetch -q $mirR --no-tags "+refs/heads/main:refs/import/reader/main"
  G -C $Mono merge --allow-unrelated-histories --no-ff -q refs/import/reader/main -m ("import(firmware/reader): fortebio/reader@{0} - lich su day du duoi firmware/reader/" -f $rSha)
  G -C $Mono update-ref -d refs/import/reader/main
  $verR = VersionFrom (Join-Path $Mono "firmware\reader\src\define.h") 'FirmwareVer\s*=\s*\{?\s*"(v[^"]+)"'
  if ($verR) { G -C $Mono tag -a "fw/reader/$verR" -m ("FirmwareVer doc tu src/define.h tai commit import; nguon fortebio/reader@{0}" -f $rSha) HEAD }
  else { Say "  CANH BAO: khong doc duoc FirmwareVer cua reader - khong tag" "Yellow" }
}

# ------------------------------------------------------------------------------------------
Step "Repo C (Production, 0 commit): copy sach -> firmware/rapidplus-prod"
$dstC = Join-Path $Mono "firmware\rapidplus-prod"
if ($haveC) { Say "  bo qua (da co)" "DarkGray" } else {
  Write-Host ("  robocopy `"{0}`" `"{1}`" /E /XD .git .pio .vscode /XF *.bin *.elf *.map" -f $SrcC, $dstC) -ForegroundColor DarkGray
  if (-not $DryRun) {
    & robocopy $SrcC $dstC /E /XD .git .pio .vscode /XF *.bin *.elf *.map /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy loi (exit $LASTEXITCODE)" }
    G -C $Mono add firmware/rapidplus-prod
    G -C $Mono commit -q -m "import(firmware/rapidplus-prod): scaffold IEC 62304 v0.1.0 (nguon FBT-RapidPlus-Production chua co commit, khong lich su)"
  }
}

# ------------------------------------------------------------------------------------------
Step "Tong ket"
if (-not $DryRun) {
  & git -C $Mono log --oneline --first-parent | Select-Object -First 8
  Say ("  tong commit: {0}" -f (& git -C $Mono rev-list --count HEAD))
  Say ("  tags: " + ((& git -C $Mono tag -l "fw/*") -join ", ") + " (+ " + @(& git -C $Mono tag -l "archive/*").Count + " tag archive)")
  Say ("  refs/archive: {0}" -f @(& git -C $Mono for-each-ref refs/archive/).Count)
  Say ("  KIEM lich su file: git -C <mono> log --oneline -- firmware/rapidplus/src/define.h | measure  (phai nhieu commit)")
  Say ("  KIEM: git -C <mono> log --all --oneline -- firmware/rapidplus/src/secrets.h  (phai rong)")
  if (Test-Path $tokFile) { Say ("  KIEM: git -C <mono> grep -I -l -F -f {0} `$(git rev-list --all)  (phai rong) roi XOA file do" -f $tokFile) "Yellow" }
  Say "  Tiep: commit fix(paths) -> docs: tach CLAUDE.md -> ci: -> chay bo kiem chung (docs/plan/monorepo-mot-he-thong.md)" "Green"
} else { Say "DRY RUN xong - khong thay doi gi." "Magenta" }
