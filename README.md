# OmniLite Standalone

Eigenständiger OmniLite-Indexer, der gegen jeden aktuellen `litecoind` läuft, statt an einen
gepatchten Litecoin Core gebunden zu sein. Einzelanwender sollen damit auch Omni-Transaktionen senden
können, über die Wallet ihres `litecoind`. Details und Phasen: [Plan.md](Plan.md).

**Stand:** Phase 0 und 1. Der Indexer folgt der Chain ab dem Omni-Genesis-Block und behandelt Reorgs;
Omni-Transaktionen wertet er noch nicht aus.
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
./build/omnilite-standalone          # synchronisieren (Strg+C beendet)
```

Konfiguration: `~/.omnilite-standalone/omnilite-standalone.conf`, Vorlage in
[omnilite-standalone.conf.example](omnilite-standalone.conf.example). Ohne `rpcpassword` wird das Cookie
von `litecoind` gelesen (`~/.litecoin/.cookie`, bei Testnet und Regtest im Unterordner).

## Herkunft

Bausteine aus Litecoin Core und OmniLite liegen in `vendor/core`, siehe [vendor/VENDOR.md](vendor/VENDOR.md).
Die erste Fassung in Go ist archiviert: [tbuuol/omnilite-standalone-go](https://github.com/tbuuol/omnilite-standalone-go).

## Lizenz

MIT, siehe [COPYING](COPYING).
