#Requires -Version 5.1
# UTF-8
# Sync-NpcFlinch.ps1: 小工具視窗，把 npc_broad.sql 的 no_damage 欄位（"是否受傷動作
# 改為噴血特效"）對照 npc_broad_fill_sprite_id.sql 算好的 id->sprite_id（sprite_id
# 就是 NpcFlinch.xml 的 Sprite id，不用再另外查 npc.sql 的 gfxid 欄位），合併寫進
# tools\ui_sample\NpcFlinch.xml（見 HitFlinchPatch.cpp 的 LoadCombatConfig()），
# 可以直接在這裡按鈕重新打包+部署 ui.pak/ui.idx（呼叫同資料夾的 Pack-UiAssets.ps1）。
#
# 2026-09-02 語意簡化（需求方指示）：拿掉 suppressFlinch 屬性，presence 本身就是
# 旗標——只有 no_damage=1（要跳過受身、改噴血）的 sprite_id 才會被「寫進」這份
# 檔案；no_damage=0（要維持原本受身動畫）的完全不寫，靠「沒列在表裡」表示。所以
# 這支工具現在只把「決議結果=要跳過受身」的 sprite_id 寫進去，其餘一律省略。
#
# 只「合併」不「整份覆蓋」：NpcFlinch.xml 裡任何目前這次資料庫查不到對應 sprite_id
# 的既有 <Sprite> 項目（例如手動測試、資料庫還沒建檔的怪）一律原樣保留，只有這次
# 資料庫能算出結果的 sprite_id 才會被寫入/覆蓋。
#
# 已知會發生的情況：同一個 sprite_id 被兩筆不同 id/npc_id 對到、但 no_damage 互相
# 矛盾（例如 97149 跟 99691 都是同一隻「阿勒尼亞」sprite_id=10632，一個
# no_damage=0、一個=1）。實測過全部 69 組衝突都是同一種規律：舊資料 no_damage=0，
# npc_note 帶「#新編號」的新資料一律 no_damage=1——所以衝突時自動採用「#新編號」
# 那筆，不用逐筆手動勾選；真的不符合這個規律的意外衝突才會落到「需人工確認」，
# 預設不寫入，畫面上自己勾。
param(
    [string] $FillSpriteIdSqlPath = (Join-Path $PSScriptRoot "..\npc_broad_fill_sprite_id.sql"),
    [string] $NpcBroadSqlPath = (Join-Path $PSScriptRoot "..\npc_broad.sql"),
    [string] $NpcFlinchXmlPath = (Join-Path $PSScriptRoot "ui_sample\NpcFlinch.xml"),
    [string] $PackScriptPath = (Join-Path $PSScriptRoot "Pack-UiAssets.ps1"),
    [string] $UiSourceFolder = (Join-Path $PSScriptRoot "ui_sample"),
    [string] $UiOutputFolder = (Join-Path $PSScriptRoot "ui_sample\_packed"),
    [string] $DeployUiDir = "D:\天堂資料\天堂專案#380客戶端+自製登入器\ui"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName System.Xaml

# ---------------------------------------------------------------------------
# 極簡 SQL VALUES(...) 逐欄位切割：只認單引號字串（''轉義成單引號）跟 null，
# 不處理巢狀括號/跳脫字元以外的情況——這份工具只吃 Navicat 匯出的固定格式。
function Split-SqlTuple {
    param([string] $Inner)
    $fields = New-Object System.Collections.Generic.List[object]
    $i = 0
    $len = $Inner.Length
    while ($i -lt $len) {
        while ($i -lt $len -and ($Inner[$i] -eq ' ' -or $Inner[$i] -eq ',')) { $i++ }
        if ($i -ge $len) { break }
        if ($Inner[$i] -eq "'") {
            $i++
            $sb = New-Object System.Text.StringBuilder
            while ($i -lt $len) {
                if ($Inner[$i] -eq "'" -and $i + 1 -lt $len -and $Inner[$i + 1] -eq "'") {
                    [void]$sb.Append("'"); $i += 2; continue
                }
                if ($Inner[$i] -eq "'") { $i++; break }
                [void]$sb.Append($Inner[$i]); $i++
            }
            $fields.Add($sb.ToString())
        } else {
            $start = $i
            while ($i -lt $len -and $Inner[$i] -ne ',') { $i++ }
            $tok = $Inner.Substring($start, $i - $start).Trim()
            if ($tok -eq "null") { $fields.Add($null) } else { $fields.Add($tok) }
        }
        while ($i -lt $len -and $Inner[$i] -ne ',') { $i++ }
        if ($i -lt $len) { $i++ }
    }
    return $fields
}

function Get-InsertTuples {
    param([string] $Content, [string] $TableName)
    # Navicat 匯出的 CREATE TABLE 用 `table` 反引號，但 INSERT INTO 這行實測沒有
    # 反引號（純 "INSERT INTO npc_broad VALUES (...)"），反引號設成可有可無以防萬一。
    $pattern = "INSERT INTO ``?$TableName``? VALUES \((.*?)\);"
    $tupleMatches = [regex]::Matches($Content, $pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($m in $tupleMatches) {
        $out.Add((Split-SqlTuple -Inner $m.Groups[1].Value))
    }
    return $out
}

# UPDATE `npc_broad` SET `sprite_id`=1572 WHERE `id`=108 AND `npc_id`=45338; -- 巨大鱷魚
# 已經是算好的 id->sprite_id 結果（照 npc_broad.npc_id = npc.npcid 對出 npc.gfxid），
# 反引號在這份檔案裡實測有出現，正規表示式仍照可有可無處理以防萬一。
function Get-FillSpriteIdMap {
    param([string] $Content)
    $pattern = "UPDATE ``?npc_broad``? SET ``?sprite_id``?=(\d+) WHERE ``?id``?=(\d+) AND ``?npc_id``?=(\d+);"
    $map = @{}
    foreach ($m in [regex]::Matches($Content, $pattern)) {
        $map[$m.Groups[2].Value] = [pscustomobject]@{ SpriteId = $m.Groups[1].Value; NpcId = $m.Groups[3].Value }
    }
    return $map
}

# ---------------------------------------------------------------------------
Write-Host "讀取 npc_broad_fill_sprite_id.sql / npc_broad.sql ..." -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath $FillSpriteIdSqlPath)) { throw "找不到 npc_broad_fill_sprite_id.sql: $FillSpriteIdSqlPath" }
if (-not (Test-Path -LiteralPath $NpcBroadSqlPath)) { throw "找不到 npc_broad.sql: $NpcBroadSqlPath" }

$fillContent = Get-Content -LiteralPath $FillSpriteIdSqlPath -Raw -Encoding UTF8
$broadContent = Get-Content -LiteralPath $NpcBroadSqlPath -Raw -Encoding UTF8

$idToSprite = Get-FillSpriteIdMap -Content $fillContent
Write-Host "  npc_broad_fill_sprite_id.sql: $($idToSprite.Count) 筆 id->sprite_id"

# npc_broad: id(0) npc_id(1) npc_note(2) send_type(3) death_chat(4) sort(5) no_damage(6)
$broadRows = New-Object System.Collections.Generic.List[object]
$orphan = New-Object System.Collections.Generic.List[object]
foreach ($t in (Get-InsertTuples -Content $broadContent -TableName "npc_broad")) {
    if ($t.Count -lt 7) { continue }
    $rowId = $t[0]
    $npcId = $t[1]
    $note = $t[2]
    $noDamage = $t[6]
    if (-not $idToSprite.ContainsKey($rowId)) {
        $orphan.Add([pscustomobject]@{ Id = $rowId; NpcId = $npcId; Note = $note; NoDamage = $noDamage; Reason = "fill 檔查不到這個 id 的 sprite_id" })
        continue
    }
    $broadRows.Add([pscustomobject]@{
        Id       = $rowId
        NpcId    = $npcId
        Note     = $note
        NoDamage = $noDamage
        Gfxid    = $idToSprite[$rowId].SpriteId
    })
}
Write-Host "  npc_broad.sql: $($broadRows.Count) 筆可對到 sprite_id，$($orphan.Count) 筆略過"
foreach ($o in $orphan) {
    Write-Host "    略過 id=$($o.Id) npc_id=$($o.NpcId) [$($o.Note)] no_damage=$($o.NoDamage) -- $($o.Reason)"
}

# 依 sprite_id 分組；同組 no_damage 不一致時，優先採用 npc_note 帶「#新編號」的那
# 筆（實測 69 組衝突全部符合：舊資料 no_damage=0，新編號版本一律 no_damage=1）。
# 真的不符合這個規律的意外衝突，還是交給人工決定，預設不勾選寫入。
$bySprite = $broadRows | Group-Object Gfxid
$reviewRows = New-Object System.Collections.Generic.List[object]
foreach ($g in $bySprite) {
    # @(...) 強制包成陣列：Where-Object/Select-Object 管線只剩 0 或 1 個結果時
    # PowerShell 會自動「拆包」成單一物件而不是陣列，導致 .Count/索引行為跟預期
    # 不一樣（曾經在這裡吃過虧，這次全部明確 @() 包起來，不依賴自動拆包行為）。
    $group = @($g.Group)
    $distinctNoDamage = @($group | Select-Object -ExpandProperty NoDamage -Unique)
    $conflict = $distinctNoDamage.Count -gt 1
    $namesJoined = ($group | ForEach-Object { "id=$($_.Id),npc_id=$($_.NpcId),no_damage=$($_.NoDamage),$($_.Note)" }) -join "; "
    $suppress = $false
    $resolvedBy = "唯一值"
    $autoResolved = $true
    # 「#新編號」是不是這組資料裡比較新/正確的版本，衝突判斷跟註解都會用到，先
    # 統一算一次。
    $newer = @($group | Where-Object { $_.Note -like "*#新編號*" })
    if (-not $conflict) {
        $suppress = ($distinctNoDamage[0] -eq "1")
    } else {
        $newerDistinct = @($newer | Select-Object -ExpandProperty NoDamage -Unique)
        if ($newer.Count -gt 0 -and $newerDistinct.Count -eq 1) {
            $suppress = ($newerDistinct[0] -eq "1")
            $resolvedBy = "採用「#新編號」"
        } else {
            $suppress = $false
            $resolvedBy = "無法自動判斷，需人工確認"
            $autoResolved = $false
        }
    }
    # 寫進 XML 的行內註解：只列「#新編號」那幾筆的 npc_id + 名稱（沒有 #新編號
    # 版本的話，才退回列出原本那筆），不重複列舊資料。
    $commentSource = if ($newer.Count -gt 0) { $newer } else { $group }
    $nameComment = (($commentSource | ForEach-Object { "$($_.NpcId) $($_.Note)" } | Select-Object -Unique) -join "; ")
    # 語意簡化後：只有決議結果=要跳過受身（$suppress -eq $true）的才寫進
    # NpcFlinch.xml；要維持原本受身動畫的完全不寫（靠「沒列在表裡」表示，見檔頭
    # 註解），所以這裡的 Include 除了要自動判斷成功，還得決議是「跳過受身」。
    $reviewRows.Add([pscustomobject]@{
        Include        = ($autoResolved -and $suppress)
        Gfxid          = [string]$g.Name
        SuppressFlinch = $suppress
        Conflict       = $conflict
        ResolvedBy     = $resolvedBy
        Detail         = $namesJoined
        NameComment    = $nameComment
    })
}
$reviewRows = $reviewRows | Sort-Object { [int]$_.Gfxid }

# ---------------------------------------------------------------------------
# 既有 NpcFlinch.xml：保留檔頭註解 + 解析既有 <Sprite> 供「這次資料庫沒算出來的
# 保留原樣」＋畫面下方對照顯示用。
function Read-ExistingFlinch {
    param([string] $Path)
    $header = ""
    $entries = New-Object System.Collections.Generic.List[object]
    if (Test-Path -LiteralPath $Path) {
        $lines = Get-Content -LiteralPath $Path -Encoding UTF8
        $headerLines = New-Object System.Collections.Generic.List[string]
        foreach ($line in $lines) {
            # 忽略 <Sprites>/</Sprites> 包裝標籤（不是這支工具寫的格式，之前有人
            # 手動加過，這裡讀到就跳過，不會被當成標頭也不會被當成資料列，重新
            # 寫檔時就順便清掉了）。
            if ($line.Trim() -eq "<Sprites>" -or $line.Trim() -eq "</Sprites>") { continue }
            # id/bloodEffect 各自獨立抓，不要求特定順序或 suppressFlinch 屬性
            # 存在——照 HitFlinchPatch.cpp 那邊 strstr 各自找的邏輯，舊格式（還
            # 帶 suppressFlinch 屬性）的既有項目也能正常讀到、重寫成新格式。
            if ($line -match '<Sprite\s') {
                $idMatch = [regex]::Match($line, 'id="(\d+)"')
                if (-not $idMatch.Success) { continue }
                $bloodMatch = [regex]::Match($line, 'bloodEffect="(\d+)"')
                $bloodEffect = if ($bloodMatch.Success) { $bloodMatch.Groups[1].Value } else { "1248" }
                $commentMatch = [regex]::Match($line, '<!--\s*(.*?)\s*-->')
                $comment = if ($commentMatch.Success) { $commentMatch.Groups[1].Value } else { "" }
                $entries.Add([pscustomobject]@{ Id = $idMatch.Groups[1].Value; BloodEffect = $bloodEffect; Comment = $comment })
            } elseif ($entries.Count -eq 0) {
                $headerLines.Add($line)
            }
        }
        $header = ($headerLines -join "`n")
    }
    if (-not $header) {
        $header = @'
<!-- NpcFlinch.xml: 每種怪物受身/血效果設定，打包進 ui.pak（見 Pack-UiAssets.ps1），
     由 HitFlinchPatch.cpp 的 LoadCombatConfig() 讀取。
     id = 客戶端 sprite/gfxid（不是伺服器端 npcid，客戶端物件本身沒有這個欄位）。
     有列在這裡 = 跳過受身、直接播血效果；沒列到 = 維持原本的後仰受身動作（預設行為）。
     不需要另外的開關屬性，presence 本身就是旗標。
     本表由 tools\Sync-NpcFlinch.ps1 自動同步 npc_broad.sql 的 no_damage=1 名單維護，手動加的項目會被保留。 -->
'@
    }
    return [pscustomobject]@{ Header = $header; Entries = $entries }
}
$existing = Read-ExistingFlinch -Path $NpcFlinchXmlPath
$existingIds = @{}
foreach ($e in $existing.Entries) { $existingIds[$e.Id] = $e }

$manualOnlyRows = New-Object System.Collections.Generic.List[object]
$syncGfxSet = @{}
foreach ($r in $reviewRows) { $syncGfxSet[$r.Gfxid] = $true }
foreach ($e in $existing.Entries) {
    if (-not $syncGfxSet.ContainsKey($e.Id)) {
        $manualOnlyRows.Add([pscustomobject]@{ Gfxid = $e.Id; Note = "手動維護，這次資料庫沒有對應資料，會保留" })
    }
}

# ---------------------------------------------------------------------------
[xml]$xaml = @"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="NpcFlinch 同步工具" Height="720" Width="980" WindowStartupLocation="CenterScreen"
        Background="#1E1E1E">
    <Grid Margin="10">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="120"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <TextBlock Grid.Row="0" Foreground="White" FontSize="14" FontWeight="Bold" Margin="0,0,0,8"
                   Text="npc_broad.sql (no_damage) + npc_broad_fill_sprite_id.sql (sprite_id) → NpcFlinch.xml（只寫 no_damage=1 要跳過受身的）"/>

        <DataGrid Name="SyncGrid" Grid.Row="1" AutoGenerateColumns="False" CanUserAddRows="False"
                  Background="#121212" Foreground="White" RowBackground="#1A1A1A" AlternatingRowBackground="#202020"
                  GridLinesVisibility="Horizontal" HeadersVisibility="Column">
            <DataGrid.Columns>
                <DataGridCheckBoxColumn Header="寫入" Binding="{Binding Include, Mode=TwoWay}" Width="50"/>
                <DataGridTextColumn Header="sprite_id" Binding="{Binding Gfxid}" Width="80" IsReadOnly="True"/>
                <DataGridCheckBoxColumn Header="決議=跳過受身" Binding="{Binding SuppressFlinch}" Width="120" IsReadOnly="True"/>
                <DataGridTextColumn Header="衝突" Binding="{Binding Conflict}" Width="60" IsReadOnly="True"/>
                <DataGridTextColumn Header="解決方式" Binding="{Binding ResolvedBy}" Width="150" IsReadOnly="True"/>
                <DataGridTextColumn Header="來源明細" Binding="{Binding Detail}" Width="*" IsReadOnly="True"/>
            </DataGrid.Columns>
        </DataGrid>

        <TextBlock Grid.Row="2" Foreground="#FFA500" FontSize="12" Margin="0,8,0,4"
                   Text="只有決議=跳過受身（no_damage=1）的才會勾「寫入」；決議=維持受身的不寫進 XML（靠沒列在表裡表示）。衝突已自動採用「#新編號」那筆，不符合這個規律的意外衝突才會標「需人工確認」並預設不勾選，請自行決定。"/>

        <GroupBox Grid.Row="3" Header="這次沒對到資料、會原樣保留的既有項目" Foreground="#B0B0B0" Margin="0,4,0,0">
            <DataGrid Name="ManualGrid" AutoGenerateColumns="False" CanUserAddRows="False" IsReadOnly="True"
                      Background="#121212" Foreground="White" RowBackground="#1A1A1A" HeadersVisibility="Column">
                <DataGrid.Columns>
                    <DataGridTextColumn Header="gfxid" Binding="{Binding Gfxid}" Width="70"/>
                    <DataGridTextColumn Header="說明" Binding="{Binding Note}" Width="*"/>
                </DataGrid.Columns>
            </DataGrid>
        </GroupBox>

        <StackPanel Grid.Row="4" Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,10,0,0">
            <TextBlock Name="StatusText" Foreground="#B0B0B0" VerticalAlignment="Center" Margin="0,0,16,0"/>
            <Button Name="WriteBtn" Content="寫入 NpcFlinch.xml" Width="160" Height="34" Margin="0,0,10,0"/>
            <Button Name="DeployBtn" Content="打包並部署 ui.pak" Width="160" Height="34"/>
        </StackPanel>
    </Grid>
</Window>
"@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)

$syncGrid = $window.FindName("SyncGrid")
$manualGrid = $window.FindName("ManualGrid")
$statusText = $window.FindName("StatusText")
$writeBtn = $window.FindName("WriteBtn")
$deployBtn = $window.FindName("DeployBtn")

$syncGrid.ItemsSource = $reviewRows
$manualGrid.ItemsSource = $manualOnlyRows
$conflictCount = ($reviewRows | Where-Object Conflict).Count
$needsReviewCount = ($reviewRows | Where-Object { $_.ResolvedBy -eq "無法自動判斷，需人工確認" }).Count
$willWriteCount = ($reviewRows | Where-Object Include).Count
$keepFlinchCount = $reviewRows.Count - $willWriteCount - $needsReviewCount
$statusText.Text = "fill 檔 $($idToSprite.Count) 筆 / npc_broad.sql 可對照 $($broadRows.Count) 筆，合併成 $($reviewRows.Count) 個 sprite_id（$conflictCount 個衝突）：$willWriteCount 個會寫入（跳過受身）、$keepFlinchCount 個不寫（維持受身）、$needsReviewCount 個需人工確認 / 略過 $($orphan.Count) 筆（明細見主控台輸出）"

function Write-NpcFlinchXml {
    $merged = @{}
    foreach ($e in $existing.Entries) { $merged[$e.Id] = [pscustomobject]@{ Id = $e.Id; BloodEffect = $e.BloodEffect; Comment = $e.Comment } }
    foreach ($row in $reviewRows) {
        if ($row.Include) {
            $merged[$row.Gfxid] = [pscustomobject]@{ Id = $row.Gfxid; BloodEffect = "1248"; Comment = $row.NameComment }
        } elseif ($row.ResolvedBy -ne "無法自動判斷，需人工確認" -and $merged.ContainsKey($row.Gfxid)) {
            # 這個 sprite_id 這次資料庫確定算出來的結論是「維持受身」，如果之前
            # NpcFlinch.xml 裡還留著它（例如上一版本手動/資料還沒更新前寫進去
            # 的），要主動刪掉——語意簡化後「有列在表裡」本身就代表跳過受身，
            # 留著沒清掉的話行為會錯（變成一直跳過受身）。真的無法自動判斷、需
            # 人工確認的衝突不動既有值，避免亂猜把原本對的值改錯。
            $merged.Remove($row.Gfxid)
        }
    }
    $sortedIds = $merged.Keys | Sort-Object { [int]$_ }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add($existing.Header)
    foreach ($id in $sortedIds) {
        $e = $merged[$id]
        $line = "<Sprite id=`"$($e.Id)`" bloodEffect=`"$($e.BloodEffect)`" />"
        if ($e.Comment) { $line += " <!-- $($e.Comment) -->" }
        $lines.Add($line)
    }
    $noBomUtf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllLines($NpcFlinchXmlPath, $lines, $noBomUtf8)
    return $merged.Count
}

$writeBtn.Add_Click({
    try {
        $count = Write-NpcFlinchXml
        $statusText.Text = "已寫入 NpcFlinch.xml，共 $count 筆（含保留的手動項目）"
        $statusText.Foreground = "LightGreen"
    } catch {
        $statusText.Text = "寫入失敗：$($_.Exception.Message)"
        $statusText.Foreground = "Red"
    }
})

$deployBtn.Add_Click({
    try {
        Write-NpcFlinchXml | Out-Null
        & powershell -NoProfile -ExecutionPolicy Bypass -File $PackScriptPath -SourceFolder $UiSourceFolder -OutputFolder $UiOutputFolder
        if ($LASTEXITCODE -ne 0) { throw "Pack-UiAssets.ps1 失敗" }
        Copy-Item -LiteralPath (Join-Path $UiOutputFolder "ui.pak") -Destination (Join-Path $DeployUiDir "ui.pak") -Force
        Copy-Item -LiteralPath (Join-Path $UiOutputFolder "ui.idx") -Destination (Join-Path $DeployUiDir "ui.idx") -Force
        $statusText.Text = "已寫入並打包部署到 $DeployUiDir"
        $statusText.Foreground = "LightGreen"
    } catch {
        $statusText.Text = "打包/部署失敗：$($_.Exception.Message)（遊戲開著時 ui.pak 可能鎖檔，先關遊戲再試）"
        $statusText.Foreground = "Red"
    }
})

$window.ShowDialog() | Out-Null
