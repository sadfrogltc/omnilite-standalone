# OmniLite Standalone

Eigenständiger OmniLite-Indexer, der gegen jeden aktuellen `litecoind` läuft, statt an einen
gepatchten Litecoin Core gebunden zu sein. Einzelanwender sollen damit auch Omni-Transaktionen senden
können, über die Wallet ihres `litecoind`. Details und Phasen: [Plan.md](Plan.md).

**Stand:** Phase 0 bis 9. Der Indexer folgt der Chain ab dem Omni-Genesis-Block und lässt jeden Block
durch OmniLite 0.18.1 selbst laufen: alle Transaktionstypen, Guthaben, Properties, DEx, Crowdsales,
NFTs, die Sicherung des States und Reorgs. Die API beantwortet die `omni_*`-Aufrufe wie `omnilitecored`, unbestätigte Omni-Transaktionen aus dem Mempool der Node eingeschlossen. `omni_send` und die übrigen Sende-Aufrufe bauen die Transaktion aus Outputs der Absenderadresse und lassen die Wallet des `litecoind` signieren und senden; `omni_setautocommit false` gibt sie stattdessen nur zurück. `omni_listtransactions` und `omni_getwallet(address)balances` zeigen die Omni-Transaktionen und Guthaben der Node-Wallet, `ismine` in den Transaktionen bezieht sich auf sie. Jede Omni-Transaktion geht vor dem Senden probehalber durch OmniLites Logik; was OmniLite ablehnen würde, wird nicht gesendet. Eine DEx-Zahlung (`omni_senddexpay`) gibt es nur auf ausdrücklichen Aufruf; ein Wächter lehnt sie ab, wenn sie zu spät käme oder OmniLite sie nicht als Kauf verbuchen würde (auf Testnet und Regtest immer). Wer das API-Login hat, kann damit Guthaben der Node-Wallet ausgeben.
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
./build/omnilite-standalone -status  # Stand des laufenden Programms (Exit-Code 0, wenn synchron)
```

`-status` fragt den Aufruf `omnilite_getstatus` der API ab: Omni-Höhe, Höhe der Node, Rückstand,
letzter Node-Fehler, Reorgs seit dem Start, Laufzeit. Er antwortet auch, während OmniLite noch seinen
State lädt.

Die API lauscht auf `127.0.0.1:9432` (Testnet 19432, Regtest 19543). Ohne `apipassword` liegt das Login
im Cookie `~/.omnilite-standalone/<netz>/.cookie`, das z. B. `litecoin-cli` lesen kann:

```bash
litecoin-cli -rpcport=9432 -rpccookiefile=$HOME/.omnilite-standalone/main/.cookie omni_getinfo
```

Konfiguration: `~/.omnilite-standalone/omnilite-standalone.conf`, Vorlage in
[omnilite-standalone.conf.example](omnilite-standalone.conf.example). Ohne `rpcpassword` wird das Cookie
von `litecoind` gelesen (`~/.litecoin/.cookie`, bei Testnet und Regtest im Unterordner).

## Snapshot

Ein Snapshot ist der Index samt Omni-Stand bei einem Block, als tar-Datei. Damit startet eine andere
Installation dort, statt ab dem Omni-Genesis-Block alles neu zu verarbeiten.

```bash
# bei gestopptem Programm:
./build/omnilite-standalone -exportsnapshot=omni-snapshot.tar
# auf dem anderen Rechner, mit dem Consensus-Hash, den der Export ausgegeben hat:
./build/omnilite-standalone -importsnapshot=omni-snapshot.tar -snapshothash=<hash>
```

Der Import berechnet den Consensus-Hash aus dem importierten Stand neu und importiert nur, wenn er dem
angegebenen entspricht; der Wert in der Datei selbst zählt nicht. Den Hash also auf einem getrennten Weg
weitergeben oder mit `omni_getcurrentconsensushash` einer vertrauenswürdigen Node vergleichen. Außerdem
muss der Block des Snapshots in der Chain der Node liegen, und `<datadir>/<netz>` muss leer sein.

## Als Dienst (systemd)

Die Unit [contrib/systemd/omnilite-standalone.service](contrib/systemd/omnilite-standalone.service)
startet das Programm beim Booten als eigener Benutzer `omnilite`, mit den Daten in
`/var/lib/omnilite-standalone`, der Konfiguration in `/etc/omnilite-standalone` und abgeschottet vom
Rest des Systems (kein Zugriff auf `/home`, sonst nur lesend).

```bash
sudo cmake --install build --prefix /usr/local
sudo useradd --system --home-dir /var/lib/omnilite-standalone --shell /usr/sbin/nologin omnilite
sudo install -d -m 0750 -g omnilite /etc/omnilite-standalone
sudo install -m 0640 -g omnilite omnilite-standalone.conf.example /etc/omnilite-standalone/omnilite-standalone.conf
sudoedit /etc/omnilite-standalone/omnilite-standalone.conf
sudo install -m 0644 contrib/systemd/omnilite-standalone.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now omnilite-standalone
```

Das Cookie von `litecoind` kann der Dienst nicht lesen (es gehört dem Benutzer der Node, und `/home`
ist gesperrt). Darum in der Konfiguration `rpcuser` und `rpcpassword` setzen, passend zu einem
`rpcauth`-Eintrag in der `litecoin.conf` (erzeugt mit `share/rpcauth/rpcauth.py` aus Litecoin Core).

Log und Stand:

```bash
journalctl -u omnilite-standalone -f
sudo -u omnilite omnilite-standalone -datadir=/var/lib/omnilite-standalone \
  -conf=/etc/omnilite-standalone/omnilite-standalone.conf -status
```

Das API-Cookie liegt dann in `/var/lib/omnilite-standalone/<netz>/.cookie` und ist nur für `omnilite`
lesbar.

## Herkunft

Bausteine aus Litecoin Core und OmniLite liegen in `vendor/core`, siehe [vendor/VENDOR.md](vendor/VENDOR.md).
Die erste Fassung in Go ist archiviert: [tbuuol/omnilite-standalone-go](https://github.com/tbuuol/omnilite-standalone-go).

## Lizenz

MIT, siehe [COPYING](COPYING).
