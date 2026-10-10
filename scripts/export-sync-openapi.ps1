# Regenerate the protocol-only OpenAPI document. This does not implement routes.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
function Ref([string]$name) { return @{ '$ref' = "./sync-v1.schema.json#/`$defs/$name" } }
function ObjectSchema($required, $properties) {
    return @{ type='object'; additionalProperties=$false; required=@($required); properties=$properties }
}
function CanonicalObject($value) {
    if ($value -is [System.Collections.IDictionary]) {
        $ordered = [ordered]@{}
        [string[]]$keys = @($value.Keys)
        [Array]::Sort($keys, [StringComparer]::Ordinal)
        foreach ($key in $keys) { $ordered[$key] = CanonicalObject $value[$key] }
        return $ordered
    }
    if ($value -is [array]) {
        $items = @()
        foreach ($item in $value) { $items += ,(CanonicalObject $item) }
        return ,$items
    }
    return $value
}
$schemaPath = Join-Path $repo 'protocol/sync-v1.schema.json'
$schema = Get-Content $schemaPath -Raw | ConvertFrom-Json -AsHashtable
$defs = $schema['$defs']
$defs['metadata'] = ($defs.node | ConvertTo-Json -Depth 30 | ConvertFrom-Json -AsHashtable)
$defs.metadata.required = @($defs.metadata.required | Where-Object { $_ -ne 'rtf_base64' })
$defs.metadata.properties.Remove('rtf_base64')
$defs['existing_precondition'] = $defs.precondition.oneOf[1]
$defs['version'] = ObjectSchema @('epoch','revision') @{ epoch=(Ref 'uuid'); revision=(Ref 'positive64') }
$defs['vault'] = ObjectSchema @('uid','name','epoch','revision','created_at','updated_at','remark') @{
    uid=(Ref 'uid'); name=@{type='string';maxLength=128}; epoch=(Ref 'uuid'); revision=(Ref 'positive64')
    created_at=@{type='string';format='date-time'}; updated_at=@{type='string';format='date-time'}; remark=@{type='string';maxLength=2000}
}
$defs['manifest'] = ObjectSchema @('vault','nodes') @{vault=(Ref 'vault');nodes=@{type='array';maxItems=20000;items=(Ref 'metadata')}}
$defs['snapshot_result'] = ObjectSchema @('vault','snapshot') @{vault=(Ref 'vault');snapshot=(Ref 'snapshot')}
$defs['subtree_result'] = ObjectSchema @('vault','root_sync_id','source') @{vault=(Ref 'vault');root_sync_id=(Ref 'uuid');source=(Ref 'snapshot')}
$defs['subtree_write'] = ObjectSchema @('precondition','source') @{precondition=(Ref 'existing_precondition');source=(Ref 'snapshot')}
$defs['remark_write'] = ObjectSchema @('precondition','remark') @{precondition=(Ref 'existing_precondition');remark=@{type='string';maxLength=2000}}
$defs['delete_write'] = ObjectSchema @('precondition') @{precondition=(Ref 'existing_precondition')}
$defs['password_write'] = ObjectSchema @('old_password','new_password') @{old_password=@{type='string';minLength=8;maxLength=128};new_password=@{type='string';minLength=8;maxLength=128}}
$defs['operation_result'] = ObjectSchema @('operation_id','status','uid','epoch','revision','created','updated','deleted') @{
    operation_id=(Ref 'uuid');status=@{enum=@('committed')};uid=(Ref 'uid');epoch=(Ref 'uuid');revision=(Ref 'positive64')
    created=@{type='integer';minimum=0;maximum=20000};updated=@{type='integer';minimum=0;maximum=20000};deleted=@{type='integer';minimum=0;maximum=20000}
}
$defs['login_result'] = ObjectSchema @('account_id','username','token','expires_at') @{
    account_id=(Ref 'positive64');username=@{type='string';pattern='^[a-z0-9_]{3,32}$'}
    token=@{type='string';pattern='^[A-Za-z0-9_-]{43}$'};expires_at=@{type='string';format='date-time'}
}
$defs['vault_list'] = ObjectSchema @('vaults','next_cursor') @{
    vaults=@{type='array';maxItems=100;items=(Ref 'vault')};next_cursor=@{oneOf=@(@{type='null'},@{type='string';maxLength=512})}
}
$defs['capabilities'] = ObjectSchema @('protocol_version','schema_version','limits') @{
    protocol_version=@{const=1};schema_version=@{const=1};limits=(ObjectSchema @('nodes','depth','node_bytes','vault_bytes','json_bytes','account_vaults','account_bytes') @{
        nodes=@{type='integer';minimum=1};depth=@{type='integer';minimum=1};node_bytes=@{type='integer';minimum=1}
        vault_bytes=@{type='integer';minimum=1};json_bytes=@{type='integer';minimum=1};account_vaults=@{type='integer';minimum=1};account_bytes=(Ref 'positive64')
    })
}
# All schema references inside the schema itself are local.
$serialized = (CanonicalObject $schema) | ConvertTo-Json -Depth 60
$serialized.Replace('./sync-v1.schema.json#/', '#/') | Set-Content $schemaPath -Encoding utf8
$paths = [ordered]@{}
function Route([string]$path, [string]$method, [string]$id, [string]$request, [string]$response, [bool]$anonymous=$false) {
    $properties = @{request_id=(Ref 'uuid')}
    $required = @('request_id')
    if ($response) { $properties['data']=(Ref $response); $required += 'data' }
    $operation = [ordered]@{
        operationId=$id
        responses=@{
            '200'=@{description='Success; a mutation is committed only after the database transaction completes.';content=@{'application/json'=@{schema=(ObjectSchema $required $properties)}}}
            'default'=@{description='Typed failure; no SQL, paths, credentials or note content in error messages.';content=@{'application/json'=@{schema=(Ref 'error')}}}
        }
    }
    if ($anonymous) { $operation['security']=@() }
    if ($request) { $operation['requestBody']=@{required=$true;content=@{'application/json'=@{schema=(Ref $request)}}} }
    $parameters = @()
    foreach ($match in [regex]::Matches($path, '\{([^}]+)\}')) {
        $name=$match.Groups[1].Value
        $kind=if($name -eq 'uid'){'uid'}else{'uuid'}
        $parameters += @{name=$name;in='path';required=$true;schema=(Ref $kind)}
    }
    if ($method -eq 'get' -and ($path.EndsWith('/snapshot') -or $path.EndsWith('/subtree'))) {
        $parameters += @{name='epoch';in='query';required=$true;schema=(Ref 'uuid')}
        $parameters += @{name='revision';in='query';required=$true;schema=(Ref 'positive64')}
    }
    if ($id -eq 'listVaults') {
        $parameters += @{name='cursor';in='query';required=$false;schema=@{type='string';maxLength=512}}
        $parameters += @{name='limit';in='query';required=$false;schema=@{type='integer';minimum=1;maximum=100;default=100}}
    }
    if ($parameters.Count) { $operation['parameters']=$parameters }
    if (!$paths.Contains($path)) { $paths[$path]=[ordered]@{} }
    $paths[$path][$method]=$operation
}
Route '/auth/register' post register credentials '' $true
Route '/auth/login' post login credentials login_result $true
Route '/auth/logout' post logout '' ''
Route '/auth/password' post changePassword password_write ''
Route '/capabilities' get capabilities '' capabilities
Route '/vaults' get listVaults '' vault_list
Route '/vaults/{uid}/manifest' get getManifest '' manifest
Route '/vaults/{uid}/snapshot' get getSnapshot '' snapshot_result
Route '/vaults/{uid}/snapshot' put putSnapshot snapshot_write operation_result
Route '/vaults/{uid}/remark' patch updateRemark remark_write operation_result
Route '/vaults/{uid}' delete deleteVault delete_write operation_result
Route '/vaults/{uid}/nodes/{sync_id}/subtree' get getSubtree '' subtree_result
Route '/vaults/{uid}/nodes/{sync_id}/subtree' put putSubtree subtree_write operation_result
Route '/vaults/{uid}/nodes/{sync_id}/subtree' delete deleteSubtree delete_write operation_result
Route '/operations/{operation_id}' get getOperation '' operation_result
$api = [ordered]@{
    openapi='3.1.0'; info=@{title='AnyNote manual sync';version='1.0.0';description='Manual snapshot sync API implemented by the Go service and native Windows client. See the implementation report for validation scope.'}
    servers=@(@{url='https://localhost:8443/api/v1'})
    security=@(@{bearerAuth=@()})
    components=@{securitySchemes=@{bearerAuth=@{type='http';scheme='bearer'}}}
    paths=$paths
}
(CanonicalObject $api) | ConvertTo-Json -Depth 60 | Set-Content (Join-Path $repo 'protocol/openapi.json') -Encoding utf8
