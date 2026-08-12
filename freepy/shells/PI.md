# Pi launcher

`python -m shells pi` 仍是薄的 Pi pass-through；所有參數會原樣交給 Pi。當
`AGENTLOOP_PI_FACTORY` 已設定時，launcher 會自動加載 repository 內的 agentloop
extension，不必再手寫 `-e freepy/adapters/pi/pi-agentloop.ts`。

從 `freepy` 目錄啟動離線示範 factory：

```powershell
$env:AGENTLOOP_PI_FACTORY = "adapters.pi.examples.minimal_factory:create"
python -m shells pi --offline
```

```bash
AGENTLOOP_PI_FACTORY=adapters.pi.examples.minimal_factory:create \
  python -m shells pi --offline
```

進入 Pi 後可從 `/al-start demo` 開始，再用 `/al-status`、`/al-resume`、
`/al-wait 5` 與 `/al-end done` 操作獨立的 Python Round。這個 factory 完全離線，
適合先確認 lifecycle；真實工作仍應提供一個明確選擇模型、tools、workspace 與 Limits
的受信任 factory。

未設定 factory 時，launcher 不會偷偷載入 extension：

```bash
python -m shells pi --offline
```

若已自行傳入同一份 `-e`／`--extension`，launcher 也不會重複加入。其他 Pi 參數與
額外 extensions 照常傳遞。

離線檢查：

```bash
cd freepy
uv run python -m shells._checks_pi
uv run python adapters/pi/check_pi_bridge.py
```
