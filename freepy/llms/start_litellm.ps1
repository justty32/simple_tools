param([string]$Config = "$PSScriptRoot\litellm.yaml", [int]$Port = 4000)

litellm --config $Config --port $Port
exit $LASTEXITCODE
