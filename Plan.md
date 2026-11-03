# OmniLite Standalone – Plan

**OmniLite Standalone** ist ein eigenständiger OmniLite-Indexer, der **nicht** an eine bestimmte
Litecoin-Core-Version gekoppelt ist. Er verbindet sich per Standard-RPC mit einem beliebigen, aktuellen
`litecoind`, liest die Blöcke, dekodiert Omni-Transaktionen selbst, berechnet den kompletten Omni-State
(Balances, Properties, Crowdsales, DEx, NFTs, Freeze, Activations) und stellt ihn über eine API bereit.

Für Einzelanwender kann er außerdem **Omni-Transaktionen senden**. Schlüssel, Signieren und Coins bleiben
bei der **Wallet des `litecoind`**; eine eigene Wallet gibt es nicht. Als Provider (z. B. für eine
Website) läuft er als reiner Indexer.

**Kernidee:** Die Omni-Logik lebt außerhalb von Core. Core-Updates (0.21, MWEB, alles danach) betreffen
uns nur noch, wenn sich die Standard-RPC-Schnittstelle ändert – und die ist sehr stabil.

**Vorgeschichte:** Eine erste Fassung (Phase 0 bis 9) entstand in Go und liegt archiviert unter
`github.com/tbuuol/omnilite-standalone-go`. Sie wurde nie gegen eine echte Node getestet. Diese Fassung
ist in C++ wie OmniLite selbst und übernimmt deren Code, wo es geht. Die in der Go-Fassung gesammelten
Erkenntnisse über OmniLite stehen in Abschnitt 7.

---

## 1. Ziele und Nicht-Ziele

**Ziele**
- Konsens-identisch mit OmniLite 0.18.1 (`omnilitecored`) – bit-genau, nachweisbar per Consensus-Hash.
- Läuft gegen jeden `litecoind` ab 0.18 (Ziel: aktuelles 0.21.x).
- Reorg-sicher.
- `omni_*`-kompatible JSON-RPC-Schnittstelle, damit bestehende Tools und die LiteWorlds-Website nur
  den Endpoint tauschen müssen.
- Senden: alle Omni-Aktionen von `omnilitecored` über die Wallet des Nodes (`wallet=1`).
- Keine verbrannten Gebühren: Jede Omni-Tx wird vor dem Senden gegen den eigenen State geprüft.
- DEx: Zahlen ist immer eine bewusste Aktion des Nutzers, nie automatisch.

**Nicht-Ziele (vorerst)**
- Keine eigene P2P-Anbindung (nur RPC).
- Keine eigene Wallet: keine Schlüssel, kein Seed, kein Keystore.
- Keine Protokolländerungen. Wir bilden 0.18.1 nach, Fehler eingeschlossen.
- Kein Windows, solange der Linux-Build nicht stabil und getestet ist.

---

## 2. Technik

- **Sprache:** C++17, wie Litecoin Core 0.21. Build mit CMake. Ziel ist Linux (entwickelt unter Ubuntu
  26.04 in WSL2, gcc 15).
- **Übernommener Code** (`vendor/core`, MIT): die Bausteine aus Litecoin Core 0.18 im OmniLite-Repo
  (`uint256`, Serialisierung, Hashes, Script, Adressen, UniValue, libsecp256k1). Herkunft und alle
  Änderungen stehen in `vendor/VENDOR.md`. Die Omni-Logik von OmniLite (`src/omnicore`) läuft fast
  unverändert, samt ihrem eigenen State-Modell; wo sie auf Core zugreift (`cs_main`, `chainActive`,
  `CCoinsViewCache`, `gArgs`, Blockdateien), liefern Stellvertreter in `src/coreshim` die Daten, per RPC
  von der Node.
- **Bibliotheken:** libevent (HTTP-Client zur Node und RPC-Server, wie Core), LevelDB (wie OmniLite),
  Boost (Header, Boost.Test).
- **Datenquelle:** `getblock <hash> 2` liefert alle Txs. Für die Sender-Bestimmung braucht es die Prevouts
  der Inputs, also `getrawtransaction` mit `-txindex` am Node.
- **State und Reorgs wie OmniLite (entschieden):** Der Omni-State liegt im Speicher und in OmniLites
  LevelDB-Datenbanken; nach jedem Block sichert OmniLite ihn in Dateien (`MP_persist`, die letzten 200
  Blöcke). Bei einem Reorg lädt OmniLite die passende Sicherung und rechnet die folgenden Blöcke neu.
  Der eigene Store (`src/store`) führt nur die Blockliste und erkennt Reorgs.

---

## 3. Fakten aus OmniLite 0.18.1 (Referenz)

Quelle: `github.com/OmniLite/omnilite`, Branch `master` = 0.18.1, Commit `fbc27c1` vom 2022-05-18.

| Parameter | Mainnet |
|---|---|
| `GENESIS_BLOCK` | 2 010 500 (Testnet und Regtest: 101) |
| Exodus-Adresse | `LTceXoduS2cetpWJSe47M25i5oKjEccN1h` |
| Class-C-Marker | `0x6f6d6e69` ("omni") im OP_RETURN |
| NFT-Feature | ab Block 2 262 000 |
| STO v1, Freeze-Notice, NFT-Issuer-Daten | nur per Activation |
| FREEDEX | ab Genesis |
| Mainnet-Consensus-Checkpoints | keine |

**Transaktionstypen:** 0 Simple Send, 3 STO, 4 Send All, 5 Send NFT, 20 DEx Sell Offer, 22 DEx Accept,
50 Fixed Issuance, 51 Crowdsale, 53 Close Crowdsale, 54 Managed Issuance, 55 Grant, 56 Revoke,
70 Change Issuer, 71/72 Enable/Disable Freezing, 185/186 Freeze/Unfreeze, 200 Any Data, 201 NFT Data,
65533 Deactivation, 65534 Activation, 65535 Alert. Kein MetaDEx, Restricted Send ist strukturell ungültig.

**Adressen:** Sender und Referenz nur P2PKH (`L…`) und P2SH (`M…`). Bech32 (`ltc1…`) kann kein
Omni-Sender sein.

**Sender:** Class C = Adresse des ersten Inputs; Class A/B = Adresse mit der größten Input-Summe
(bei Gleichstand die kleinste, wie `std::map`). Referenz: der einzige Kandidat, sonst der letzte, wobei der
erste Output zurück an den Sender als Change übersprungen wird.

---

## 4. Phasen

Nach jeder Phase: Build und Unit-Tests unter Linux grün, Commit, Push. Der Abgleich mit einem echten
`litecoind` und `omnilitecored` kommt, sobald eine Testumgebung bereitsteht (Abschnitt 5).

### Phase 0 – Grundgerüst
- CMake-Projekt, übernommene Core-Bausteine, Config (Datei und Kommandozeile), RPC-Client über libevent,
  Prüfung auf `-txindex`.
- **Fertig, wenn:** `omnilite-standalone -check` Node-Version, Höhe und txindex-Status meldet.
- **Stand:** umgesetzt. Config wie `litecoin.conf` (`network`, `rpcconnect`, `rpcport`, `rpcuser`,
  `rpcpassword`, `rpccookiefile`, `pollinterval`, `undodepth`), Cookie von `litecoind` als Standard,
  `-check` prüft Node, Chain, Höhe, Sync, txindex (über `getindexinfo` bzw. bei älteren Nodes über den
  Coinbase-Test) und Pruning. Getestet mit einem simulierten `litecoind` über HTTP; ein echter Node steht aus.

### Phase 1 – Block-Pipeline und Reorgs
- LevelDB mit Undo-Journal, Blöcke ab Genesis lesen, Tip folgen, Reorgs zurückrollen.
- **Fertig, wenn:** Sync und simulierte Reorgs (länger, kürzer, gleiche Höhe, tiefer als `undodepth`)
  sauber laufen.
- **Stand:** umgesetzt (`src/store.*`, `src/syncer.*`). LevelDB unter `<datadir>/<network>/index`, jeder Block
  ein atomarer Batch mit Block-Eintrag, State-Änderungen und Undo-Datensatz (Wert jedes geänderten Schlüssels
  vor dem Block). Undo-Daten werden nach `undodepth` Blöcken gelöscht; ein tieferer Reorg bricht mit klarer
  Meldung ab. Der Syncer folgt dem Tip per Polling, erkennt Reorgs über `previousblockhash` und, im
  eingeholten Zustand, über den Hash an der eigenen Tip-Höhe. Node-Fehler werden geloggt und wiederholt,
  falsche Zugangsdaten beenden das Programm. Strg+C bzw. SIGTERM beenden sauber. Getestet: Store (Undo,
  Pruning, Scan, Wiedereröffnen, falsches Netz) und Syncer mit simulierter Chain (alle Reorg-Fälle,
  Wiederholung nach Node-Fehlern). Ein Sync gegen einen echten `litecoind` steht aus.

### Phase 2 – Decoder
- `parseTransaction` aus `omnicore.cpp`: Class A/B/C, Sender, Referenz, Class-B-Entschleierung, Payload,
  dazu `interpret_Transaction` aus `tx.cpp` (OmniLite speichert Txs mit kaputtem Payload gar nicht).
- **Stand:** umgesetzt. `GetEncodingClass` und `parseTransaction` stammen wörtlich aus OmniLite
  (`src/omnicore/omnicore.cpp`), ebenso `parsing`, `script`, `encoding`, `createpayload`, `rules`
  (bis `IsAllowedOutputType`) und der Strukturteil von `tx.cpp`; Herkunft und Änderungen in
  `src/omnicore/ORIGIN.md`. Statt Cores Coins-View liefert eine `InputSource` die ausgegebenen Outputs,
  im Betrieb per `getrawtransaction`. Der Indexer liest jeden Block mit `getblock` (Verbosity 2), dekodiert
  die Txs im Format von Litecoin 0.18 (Txs mit MWEB-Daten werden mit Warnung übersprungen) und speichert wie
  `mastercore_handler_tx` nur Txs, die `parseTransaction` mit 0 abschließen und `interpret_Transaction`
  bestehen. DEx-Zahlungen kommen erst mit Phase 4 in die Liste, wenn sie Token kaufen. Getestet mit den
  Decoder-Tests von OmniLite (auf Litecoin-Adressen umgerechnet, siehe `ORIGIN.md`) und eigenen
  Indexer-Tests; der Abgleich mit `omni_listblocktransactions` eines Referenz-Nodes steht aus. Die
  Datenbank aus Phase 1 muss gelöscht werden.

### Phase 3 – Omni-State (zusammen mit der früheren Phase 4)
- Alle Transaktionstypen (Send, Send All, Issuance, Grant/Revoke, Change Issuer, Any Data, Crowdsales,
  STO, DEx, Freeze, NFTs, Activations, Alerts), Sicherung des States, Reorgs, Consensus-Hash.
- In OmniLite hängen Logik, Sicherung und Reorg-Behandlung so eng zusammen, dass sie zusammen
  übernommen werden; deshalb sind die früheren Phasen 3 und 4 eine.
- **Stand:** umgesetzt. Der komplette Omni-Teil von OmniLite läuft (`src/omnicore`, siehe `ORIGIN.md`):
  `mastercore_init`, die Block- und Transaktions-Handler, alle `logicMath_*`, die Datenbanken
  (`MP_txlist`, `MP_spinfo`, `MP_stolist`, `Omni_TXDB`, `OMNI_nftdb`) und die State-Dateien
  (`MP_persist`) unter `<datadir>/<netz>/omnicore`. Der Indexer ruft die Handler in der Reihenfolge von
  Cores `ConnectTip`/`DisconnectTip` auf. Was einen Block braucht (Block, Inputs der Txs mit Marker),
  holt er vorher; ein Node-Fehler dabei lässt den State unberührt und wird wiederholt. Ein Fehler mitten
  im Block oder ein `DoAbortNode` von OmniLite (z. B. unbekanntes Feature aktiviert) beendet das
  Programm; beim Neustart lädt OmniLite seine letzte Sicherung. Getestet: 145 Unit-Tests aus OmniLite
  (Tally, DEx-Kauf, Crowdsale-Mathe, NFT-Datenbank, Activations, Alerts, Regeln, Payloads, Encoding,
  128-Bit-Arithmetik u. a.; veraltete Erwartungen angepasst, siehe `ORIGIN.md`) und eigene Tests, die
  Ausgabe, Sendung, Reorg mit Rücksprung auf die Sicherung, Neustart und Nachholen fehlender Blöcke
  durch OmniLite laufen lassen. Ein Lauf gegen einen echten Node steht aus. Die Datenbank aus Phase 2
  muss gelöscht werden.

### Phase 5 – API
- JSON-RPC-Server (libevent) mit den `omni_*`-Aufrufen im Format von `omnilitecored`, lesende
  Litecoin-Aufrufe werden an `litecoind` durchgereicht.
- **Stand:** umgesetzt. OmniLites RPC-Dateien laufen unverändert (`rpc`, `rpctxobject`, `rpcvalues`,
  `rpcrequirements`, `rpcpayload`, `rpcrawtx`, dazu `createtx`, `walletfetchtxs`): alle lesenden
  `omni_*`-Aufrufe, `omni_createpayload_*` und `omni_createrawtx_*`, dazu `help`, `stop`, `uptime`,
  `getrpcinfo`. Hilfe- und Fehlertexte stammen aus Core (`rpc/protocol`, `rpc/util` in `vendor/core`,
  `rpc/server` als Stellvertreter). Der HTTP-Teil (`src/httprpc.cpp`) folgt Cores `httprpc.cpp`: Basic
  Auth mit `apiuser`/`apipassword` oder Cookie (`<datadir>/<netz>/.cookie`, nur für den Besitzer
  lesbar), Einzel- und Batch-Anfragen, Cores HTTP-Statuscodes, `RPC_IN_WARMUP`, solange OmniLite
  seinen State lädt. Standard: `127.0.0.1`, Port 9432 (Testnet 19432, Regtest 19543). Durchgereicht an
  `litecoind` wird nur eine feste Liste lesender Aufrufe (`getblock*`, `getrawtransaction`,
  `decoderawtransaction`, `gettxout`, `getrawmempool` u. a.); die Wallet des Nodes bleibt unerreichbar.
  Ein Aufruf läuft unter `cs_main`, der Indexer hält `cs_main`, während er einen Block in OmniLite
  einspielt: die API sieht den State nie halb verbucht. Das Holen der Blöcke und Inputs läuft ohne
  Sperre. Getestet: Login, Cookie, Formate, Statuscodes, Durchreichen, Warmup und `omni_getinfo`,
  `omni_getproperty`, `omni_getbalance`, `omni_gettransaction`, `omni_listblocktransactions`,
  `omni_createpayload_simplesend` auf einem echten OmniLite-State. Die Wallet-Aufrufe
  (`omni_listtransactions`, `omni_getwalletbalances`) fehlen bis Phase 7.

### Phase 6 – Mempool und Pending
- Unbestätigte Omni-Txs, `omni_listpendingtransactions`.
- **Stand:** umgesetzt. In Core landet jede Tx, die in den Mempool kommt, über `TryToAddToMarkerCache`
  im Marker-Cache; `omni_listpendingtransactions` zeigt die Mempool-Txs aus diesem Cache. Hier fragt
  `MempoolWatcher` (`src/mempool.cpp`) im Abstand von `pollinterval` den Mempool der Node ab
  (`getrawmempool`), holt neue Txs und gibt sie an `TryToAddToMarkerCache`; Txs, die die Node nicht mehr
  hat, verlassen den Cache. Die Txids stehen im Stellvertreter `mempool` (`coreshim/txmempool.h`), den
  OmniLites `queryHashes` liest. Wie nach Cores `ConnectTip` nimmt der Indexer die Txs eines neuen
  Blocks sofort aus Mempool und Cache. Unbestätigte Txs zeigt `omni_gettransaction` mit 0
  Bestätigungen, ohne Urteil über die Gültigkeit, wie `omnilitecored`. Fällt die Node aus, bleibt der
  letzte bekannte Mempool stehen. Der Watcher läuft nur mit API. Getestet mit einer simulierten Node:
  Aufnahme, Filter nach Adresse, Bestätigung durch einen Block, Verdrängung aus dem Mempool,
  Node-Ausfall. `my_pending` (eigene gesendete Txs, vorläufige Guthaben) bleibt bis Phase 7 leer.

### Phase 7 bis 9 – Senden über die Node-Wallet
- 7: Tx-Builder (Class C/B), Dry-Run, `omni_send` und Verwandte, `omni_createpayload_*`, `omni_createrawtx_*`.
- 8: Token-Verwaltung (Issuance, Grant/Revoke, Crowdsale, Freeze, STO, NFT-Daten).
- 9: DEx (Sell, Accept, Pay) mit Zahlungs-Wächter, der nie selbst zahlt.
- **Stand Phase 7:** umgesetzt. Entscheidungen: eigener Tx-Builder über die Node-Wallet, Senden über
  die API immer an (wie `omnilitecored`), Gebühr wie die Node-Wallet. OmniLites `rpctx.cpp` läuft
  unverändert, damit sind alle `omni_send*`-Aufrufe da (auch Token-Verwaltung und DEx aus Phase 8/9,
  dort noch ungetestet), dazu `omni_setautocommit` für den Dry-Run (gibt die signierte Tx zurück statt
  sie zu senden). Was `rpctx.cpp` von Cores Wallet will, beantwortet `src/nodewallet.cpp` per RPC:
  `CWallet` ist eine Wallet der Node (Name aus der URI `/wallet/<name>`, wie `litecoin-cli
  -rpcwallet`), `SelectCoins` nimmt per `listunspent` nur vertrauenswürdige Outputs der
  Absenderadresse (bis Gebühr für 20 kB plus Referenzbetrag, wie OmniLite), `createTransaction` baut die
  Tx mit genau diesen Inputs, Wechselgeld zurück an den Absender vor dem letzten Output (der
  Referenzadresse), Wechselgeld unter der Dust-Grenze geht in die Gebühr. Signiert wird mit
  `signrawtransactionwithwallet`, gesendet mit `sendrawtransaction`; die Schlüssel verlassen die Node
  nie. Gebühr: `paytxfee` der Wallet, sonst `estimatesmartfee` (6 Blöcke), sonst 0,0001 LTC/kB, nie
  unter `relayfee`/`mempoolminfee` der Node; berechnet auf die signierte Größe. `WalletTxBuilder`
  (Class C, sonst Class B mit dem öffentlichen Schlüssel aus `getaddressinfo`) und
  `CreateFundedTransaction` (`omni_funded_send`) sind neu geschrieben, folgen aber dem Ablauf des
  Originals. Fehler der Node (z. B. gesperrte Wallet) kommen mit ihrem Text beim Aufrufer an; OmniLites
  Fehlercodes bleiben, mit Grund. Eine gesendete Tx ist sofort pending (Mempool, Marker-Cache,
  `PendingAdd`). Getestet mit einer simulierten Node-Wallet: Aufbau, Gebühr, Verbuchung durch OmniLite,
  Dry-Run, Wallet-Auswahl über die URI, gesperrte Wallet, fremde und zu kleine Outputs, Class B,
  `omni_funded_send` mit Verbuchung, ohne Wallet.
- **Stand lesende Wallet-Aufrufe:** umgesetzt. `omni_listtransactions`, `omni_getwalletbalances` und
  `omni_getwalletaddressbalances` laufen mit OmniLites Code; die Adressen der Wallet kommen aus
  `listreceivedbyaddress` (wie Cores Adressbuch, Watch-only auf Wunsch), ihre Transaktionen aus
  `listtransactions`. `ismine` in den Tx-Objekten (`omni_gettransaction`, `omni_listpendingtransactions`,
  `omni_getsto`, `omni_decodetransaction`) fragt `getaddressinfo`, einmal je Adresse und Aufruf; ohne
  passende Wallet auf der Node ist nichts „mine“ und der Aufruf funktioniert trotzdem, die Wallet-Aufrufe
  selbst melden dann den Fehler der Node. Die Reihenfolge innerhalb eines Blocks (bei OmniLite der
  Byte-Offset aus dem txindex) ist hier die Position im Block, von der Node gelesen. Dabei gefunden und
  behoben: Bool-Parameter an die Node gingen als Zahl raus (`getblockheader <hash> 1` statt `true`, ein
  echter `litecoind` lehnt das ab).
- **Stand Phase 8:** getestet, keine Änderung am Code nötig. Ein Ende-zu-Ende-Test schickt jeden Aufruf
  über die API, lässt die simulierte Node-Wallet senden, setzt die Tx in einen Block und prüft, was
  OmniLite daraus macht: Managed Property, Grant (an andere und an sich selbst), Revoke, Grants-Liste,
  Freezing an, Freeze, Unfreeze, Freezing aus, Send-to-Owners (Gebühr in OMNI), Crowdsale mit Teilnahme
  und vorzeitigem Schließen, Issuer-Wechsel, `omni_sendall`, NFT-Issuance, `omni_sendnonfungible`,
  `omni_setnonfungibledata` (Holder-Daten), `omni_sendanydata`. Ein Grant von einem Nicht-Issuer wird
  vor dem Senden abgelehnt. Freezing wirkt sofort: die Wartezeit gilt nur mit dem Feature
  „Freeze-Notice“, das auch auf Mainnet nicht aktiv ist. Issuer-Daten für NFTs sind ebenfalls ein nicht
  aktiviertes Feature und darum nicht getestet.
- **Stand Phase 9:** umgesetzt. Entscheidungen: Zahlung abgelehnt unter 3 verbleibenden Blöcken im
  Zahlungsfenster, kein Override. Sell, Accept und Cancel laufen unverändert über OmniLites
  `rpctx.cpp`; `omni_senddexpay` zahlt nur auf ausdrücklichen Aufruf und hat einen Wächter
  (`src/dexguard.cpp`), der vor dem Bauen ablehnt, wenn OmniLite hinter der Node liegt (das Restfenster
  wäre falsch gerechnet), weniger als 3 Blöcke des Fensters bleiben (die Zahlung zählt bis einschließlich
  Block Accept + Fenster), eine eigene Zahlung für denselben Accept noch unbestätigt ist, oder OmniLite die
  Zahlung (mit FREEDEX, auf Mainnet ab Genesis aktiv) einem anderen Offer des Verkäufers zuordnen bzw.
  ohne Offer gar nicht verbuchen würde – ein Cancel lässt offene Accepts stehen. Die signierte Zahlung
  geht vor dem Senden durch OmniLites Parser und muss als DEx-Zahlung des Käufers erkannt werden; auf
  Testnet und Regtest ist das nie der Fall (Exodus-Output nur für Mainnet), dort wird jede Zahlung
  abgelehnt. Überzahlung lehnt OmniLite selbst ab, und jede Zahlung kauft mindestens eine Einheit
  (OmniLite rundet auf). Getestet auf Mainnet-Parametern: Sell, Accept, Zahlung mit Verbuchung,
  Doppelzahlung, Rückstand zur Node, Restfenster, Cancel mit offenem Accept, neues Offer für einen
  anderen Token; auf Regtest die Ablehnung jeder Zahlung.
- **Stand Pflicht-Probelauf:** umgesetzt (`src/dryrun.cpp`). Jede Omni-Tx, die `WalletTxBuilder` oder
  `CreateFundedTransaction` gebaut und signiert hat, geht vor dem Senden (und vor der Rückgabe bei
  `omni_setautocommit false`) durch OmniLites Parser und Logik wie im nächsten Block: Sie muss als
  Omni-Tx erkannt werden, mit dem gewünschten Absender und Empfänger, und OmniLites Logik muss sie
  annehmen. Sonst wird sie nicht gesendet; die Fehlermeldung nennt OmniLites Code und dessen
  „rejected“-Zeile. Dafür hat `CMPTransaction` einen Schalter `dryRun`: jede Logik-Funktion endet an
  der Stelle, wo OmniLite vom Prüfen zum Buchen übergeht, mit „gültig“; bei Sell Offer und Accept,
  deren Prüfungen in `dex.cpp` mit dem Buchen verwoben sind, prüft `dryrun.cpp` dieselben
  Bedingungen lesend nach. Im normalen Blockablauf ist der Schalter aus. Nicht abgedeckt: die
  Reihenfolge mit anderen unbestätigten Txs, Activations/Alerts (sendet die API nicht). Dabei
  aufgefallen: Class-B-Txs (Payload zu groß für OP_RETURN) sieht OmniLite auf Testnet und Regtest
  nicht; sie werden dort jetzt abgelehnt statt verloren. Getestet: Freeze ohne aktiviertes Freezing
  (auch mit Dry-Run), Send-All ohne Token, doppelter Accept, Class B auf Regtest abgelehnt und auf
  Mainnet-Parametern gebaut und von OmniLite verbucht.

- **Stand Status und systemd:** umgesetzt. Entscheidungen: eigener Aufruf statt Felder in
  `omni_getinfo` (das bleibt wie bei `omnilitecored`), Systemdienst mit eigenem Benutzer.
  `omnilite_getstatus` (`src/status.cpp`) meldet Version, Netz, ob OmniLite noch lädt, Omni-Höhe mit
  Hash und Zeit, Höhe der Node, Rückstand, `synced`, Verbindung zur Node mit letztem Fehler, Reorgs
  (abgehängte Blöcke seit dem Start, letzter) und Laufzeit. Der Syncer pflegt die Werte; die API
  beantwortet den Aufruf selbst, ohne Warmup-Sperre und ohne `cs_main`, also auch beim Laden und
  während ein Block verarbeitet wird. `-status` fragt ihn über das API-Cookie ab und druckt ihn,
  Exit-Code 0 nur wenn synchron. Die Unit `contrib/systemd/omnilite-standalone.service` läuft als
  `omnilite`, Daten in `/var/lib/omnilite-standalone`, Konfiguration in `/etc/omnilite-standalone`,
  mit Sandbox (`ProtectSystem=strict`, `ProtectHome`, keine Capabilities u. a.); geprüft mit
  `systemd-analyze verify`, aber noch nicht als laufender Dienst. Zugang zur Node per
  `rpcuser`/`rpcpassword` (`rpcauth` in der `litecoin.conf`), weil der Dienst das Cookie der Node
  nicht lesen kann. `cmake --install` installiert das Programm.

- **Stand Snapshot:** umgesetzt (`src/snapshot.cpp`, `src/tar.cpp`). Entscheidungen: Export nur bei
  gestopptem Programm, Import nur mit angegebenem Consensus-Hash, tar ohne neue Abhängigkeit.
  `-exportsnapshot=<datei>` öffnet Index und OmniLite (scheitert, wenn das Programm läuft, weil die
  Datenbanken gesperrt sind), berechnet OmniLites Consensus-Hash am Tip und schreibt `<netz>/index` und
  `<netz>/omnicore` als ustar-Datei, vorneweg `SNAPSHOT.json` (Netz, Block, Hash, Zeit, Consensus-Hash,
  SHA-256 und Größe jeder Datei); LevelDB-`LOCK`/`LOG` bleiben draußen, geschrieben wird über `.part`.
  `-importsnapshot=<datei> -snapshothash=<hash>` entpackt in `<netz>.importing`, prüft jede Datei gegen
  die Liste, ob der Block in der Chain der Node liegt und die Node so weit ist, ob der Index dort endet,
  lädt OmniLite darauf und vergleicht den daraus berechneten Consensus-Hash mit dem angegebenen; erst
  dann wird der Ordner zu `<netz>`. Bei jedem Fehler bleibt nichts zurück; über vorhandene Daten wird nie
  importiert. Der tar-Leser nimmt nur normale Dateien mit relativen Pfaden (kein `..`, keine Links).
  Getestet: tar mit langen Namen und gegen das System-`tar`, unsichere Namen, kaputter Header, Links,
  abgeschnittene Datei; Export bei laufendem Programm, falscher und fehlender Hash, Block nicht in der
  Chain, Node zu weit hinten, ein geändertes Byte, erfolgreicher Import mit gleichem Stand, Import über
  vorhandene Daten.

### Später
- Windows erst, wenn Linux stabil und getestet ist.

---

## 5. Verifikation

1. **Referenz-Node:** `omnilitecored` 0.18.1 mit `-txindex`, dazu ein aktueller `litecoind`, beide unter Linux.
2. **Abgleich:** Consensus-Hash je Höhe, alle Omni-Txs per `omni_gettransaction`, Endstand (Properties,
   Balances, DEx, NFTs). Ein Vergleichsskript meldet den ersten abweichenden Block.
3. **Regtest:** gezielte Txs gegen beide Implementierungen, auch jede Tx, die unser Builder baut.
4. **Unit-Tests** aus `src/omnicore/test` übernehmen.

---

## 6. Risiken

| Thema | Risiko | Umgang |
|---|---|---|
| MWEB (Litecoin 0.21) | Txs, die Peg-out-Outputs ausgeben, sehen für 0.18 anders aus. | Nur sehen, was 0.18 sieht; gezielter Test. |
| Verlorene Gebühren | Omni-ungültige Txs landen trotzdem in der Chain. | Pflicht-Dry-Run vor jedem Broadcast. |
| DEx-Zahlung | Zu spät oder an den falschen Seller → LTC weg. | Dry-Run muss einen Kauf ergeben, Warnung kurz vor Ablauf. |
| Übernommener Code | Core-Code erwartet einen vollen Node. | Nur Bausteine übernehmen, Änderungen in `VENDOR.md` festhalten. |
| Speicherfehler | C++. | Tests auch mit `-DOMNILITE_SANITIZE=ON` (ASan, UBSan). |
| `-txindex` | Nicht jeder Node hat ihn. | Vorausgesetzt, `-check` meldet es. |

---

## 7. Eigenheiten von OmniLite (aus der Go-Fassung)

Diese Punkte wurden beim Lesen des Quellcodes gefunden und müssen genau so nachgebaut werden. Bestätigt
gegen einen echten Node ist noch keiner.

- `GetEncodingClass` erkennt den Exodus-Output nur, wenn er P2PKH ist. Die Exodus-Adressen von Testnet
  und Regtest sind P2SH (`Q…`); dort gibt es also keine Class-A/B-Transaktionen und keine DEx-Zahlungen,
  nur Class C. (Neu in der C++-Fassung gefunden; die Go-Fassung hatte das nicht.)
- Die Decoder-Tests im OmniLite-Repository erwarten noch Omni-Core-Adressen (Bitcoin) und laufen gegen
  OmniLite selbst nicht. Sie sind hier auf Litecoin umgerechnet.
- `HashToAddress` kennt nur die Versionsbytes 48 (`L`) und 5, nicht 50 (`M`). Ein Freeze an eine
  `M…`-Adresse mit Versionsbyte 50 ist strukturell ungültig; beim Senden schreiben wir deshalb 5.
- `getIssuer` vergleicht nur Blockhöhen: Ein Issuer-Wechsel später im selben Block zählt schon.
- Change Issuer auf Property 1/2 ändert nichts. Deaktivierung von Feature 18 setzt Block 999 999.
- Im Consensus-Hash wird der Verkäufer eines Offers durch Abschneiden von zwei Zeichen aus `addr-prop`
  gewonnen; ab Property 10 steht dort ein Teil der Property-ID mit drin.
- DEx-Zahlung mit FREEDEX: Es zählt nur das erste aktuelle Offer des Verkäufers.
- STO ist praktisch immer ungültig (-50027), weil es keine OMNI-/TOMNI-Tokens für die Gebühr gibt.
- NFT-Daten (Typ 201) ersetzen nur Ranges, die ab dem Start lückenlos gefunden werden.
- Unique Send prüft `GetAvailableTokenBalance` mit den Pending-Beträgen der eigenen Wallet; das ist
  knotenabhängig und gehört nicht zum Konsens.
- Accepts laufen am Blockende ab, wenn `Höhe - Accept-Block >= Payment Window`; eine Zahlung im letzten
  Block des Fensters zählt noch.
- `omni_createpayload_grant` verlangt das Memo, `RequireSameEcosystem` vergleicht die Ecosystem-Nummer
  wie eine Property-ID, `RequireSaneDExFee` nennt 0,02 LTC, prüft aber 0,01.

**Entschieden**
- Projektname **OmniLite Standalone**, Repo `github.com/tbuuol/omnilite-standalone`, Lizenz MIT.
- C++ wie OmniLite, Linux zuerst, Windows später.
- Keine eigene Wallet, Senden über die Wallet des `litecoind`. DEx-Zahlung nie automatisch.
- Absender und Empfänger nur `L…`/`M…`, neue Adressen per `getnewaddress "" p2sh-segwit`.
