$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
function Encode($value) {
    if ($null -eq $value) { return 'n' }
    if ($value -is [bool]) { if($value){return 't'}else{return 'f'} }
    if ($value -is [string]) { return 's'+[Text.Encoding]::UTF8.GetByteCount($value)+':'+$value }
    if ($value -is [System.Collections.IDictionary]) {
        [string[]]$keys=@($value.Keys)
        [Array]::Sort($keys,[Comparison[string]]{
            param($a,$b)
            $left=[Text.Encoding]::UTF8.GetBytes($a); $right=[Text.Encoding]::UTF8.GetBytes($b)
            for($i=0;$i -lt [Math]::Min($left.Length,$right.Length);$i++) { if($left[$i] -ne $right[$i]) { return [int]$left[$i]-[int]$right[$i] } }
            return $left.Length-$right.Length
        })
        $encoded='o'+$keys.Length+':'
        foreach($key in $keys) { $encoded+=(Encode $key)+(Encode $value[$key]) }
        return $encoded
    }
    if ($value -is [array]) {
        $encoded='a'+$value.Length+':'
        foreach($item in $value) { $encoded+=Encode $item }
        return $encoded
    }
    return 'i'+([long]$value).ToString([Globalization.CultureInfo]::InvariantCulture)+';'
}
$descriptors=@(
    @{method='DELETE';path='/api/v1/vaults/0123456789abcdef';precondition=@{expected_epoch='00000000-0000-4000-8000-000000000001';expected_revision='9223372036854775807'};payload=$null},
    @{method='PUT';path='/api/v1/vaults/0123456789abcdef/snapshot';precondition=@{create_only=$true};payload=@{protocol_version=1;schema_version=1;uid='0123456789abcdef';name="中文😀`0`n";nodes=@()}},
    @{method='PATCH';path='/api/v1/vaults/0123456789abcdef/remark';precondition=@{expected_epoch='00000000-0000-4000-8000-000000000001';expected_revision='1'};payload=@{remark='a:b;"<&>'}}
)
$cases=@()
foreach($descriptor in $descriptors) {
    $bytes=[Text.Encoding]::UTF8.GetBytes("AnyNote/request/v1`n"+(Encode $descriptor))
    $cases+=@{descriptor=$descriptor;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()}
}
$dir=Join-Path $repo 'tests/fixtures/request-identity'
New-Item -ItemType Directory -Force $dir | Out-Null
ConvertTo-Json -InputObject $cases -Depth 30 | Set-Content (Join-Path $dir 'v1.json') -Encoding utf8
