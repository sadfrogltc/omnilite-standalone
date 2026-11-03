# OmniLite Standalone

Eigenständiger OmniLite-Indexer, der gegen jeden aktuellen `litecoind` läuft, statt an einen
gepatchten Litecoin Core gebunden zu sein. Einzelanwender sollen damit auch Omni-Transaktionen senden
können, über die Wallet ihres `litecoind`. Details und Phasen: [Plan.md](Plan.md).

**Stand:** Phase 0 bis 7. Der Indexer folgt der Chain ab dem Omni-Genesis-Block und lässt jeden Block
durch OmniLite 0.18.1 selbst laufen: alle Transaktionstypen, Guthaben, Properties, DEx, Crowdsales,
NFTs, die Sicherung des States und Reorgs. Die API beantwortet die `omni_*`-Aufrufe wie `omnilitecored`, unbestätigte Omni-Transaktionen aus dem Mempool der Node eingeschlossen. `omni_send` und die übrigen Sende-Aufrufe bauen die Transaktion aus Outputs der Absenderadresse und lassen die Wallet des `litecoind` signieren und senden; `omni_setautocommit false` gibt sie stattdessen nur zurück. `omni_listtransactions` und `omni_getwallet(address)balances` zeigen die Omni-Transaktionen und Guthaben der Node-Wallet, `ismine` in den Transaktionen bezieht sich auf sie. Wer das API-Login hat, kann damit Guthaben der Node-Wallet ausgeben.
Noch nicht gegen einen echten `litecoind` getestet.

## Voraussetzungen (Linux)

```bash
sudo apt install build-essential cmake pkg-config libevent-dev libleveldb-dev libboost-dev libboost-test-dev
```

Dazu ein `litecoind` mit `-txindex=1`, nicht gepruned.

## Bauen und testen

```bash
cmake -S . -B build
cmake --build build -j
./build/test_omnilite
```

Mit `-DOMNILITE_SANITIZE=ON` laufen Build und Tests mit AddressSanitizer und UndefinedBehaviorSanitizer.

## Starten

```bash
./build/omnilite-standalone -check   # Verbindung, Chain und txindex prüfen
./build/omnilite-standalone          # synchronisieren und API starten (Strg+C beendet)
```n
Die API lauscht auf `127.0.0.1:9432` (Testnet 19432, Regtest 19543). Ohne `apipassword` liegt das Login
im Cookie `~/.omnilite-standalone/<netz>/.cookie`, das z. B. `litecoin-cli` lesen kann:

```bash
litecoin-cli -rpcport=9432 -rpccookiefile=$HOME/.omnilite-standalone/main/.cookie omni_getinfo
```

Konfiguration: `~/.omnilite-standalone/omnilite-standalone.conf`, Vorlage in
[omnilite-standalone.conf.example](omnilite-standalone.conf.example). Ohne `rpcpassword` wird das Cookie
von `litecoind` gelesen (`~/.litecoin/.cookie`, bei Testnet und Regtest im Unterordner).

## Herkunft

Bausteine aus Litecoin Core und OmniLite liegen in `vendor/core`, siehe [vendor/VENDOR.md](vendor/VENDOR.md).
Die erste Fassung in Go ist archiviert: [tbuuol/omnilite-standalone-go](https://github.com/tbuuol/omnilite-standalone-go).

## Lizenz

MIT, siehe [COPYING](COPYING).
