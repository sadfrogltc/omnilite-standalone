# coreshim

Stellvertreter für Header von Litecoin Core, die die Dateien in `src/omnicore` einbinden, deren
Original aber einen vollen Node voraussetzt. Jeder Header bildet nur das nach, was OmniLite davon
benutzt; die Umsetzung steht in `coreshim.cpp` und `policy.cpp`.

| Header | Inhalt |
|---|---|
| `chain.h` | `CBlockIndex` (Hash, Höhe, Zeit, Vorgänger) und `CChain` |
| `validation.h` | `cs_main`, `chainActive`, `mapBlockIndex`, `ReadBlockFromDisk`, `GetTransaction`, `DoAbortNode`, `DoWarning` |
| `coins.h` | `Coin` und der Teil von `CCoinsViewCache`, den OmniLite als Input-Cache nutzt |
| `primitives/block.h` | `CBlock` mit seinen Transaktionen |
| `policy/policy.h` | `GetDustThreshold`, `IsDust`, `minRelayTxFee` (Code aus Cores `policy.cpp`) |
| `util/system.h` | `gArgs` für die `-omni*`-Argumente, `GetDataDir`, `TryCreateDirectories`, `LogPrintf` |
| `util/time.h`, `sync.h`, `fs.h` | Zeit, Locks (`std::recursive_mutex`), `std::filesystem` |
| `txmempool.h` | die Txids im Mempool der Node, gefüllt von `MempoolWatcher` (`src/mempool.cpp`) |
| `wallet/wallet.h`, `wallet/coincontrol.h`, `interfaces/wallet.h` | eine Wallet der Node per RPC: Outputs, `isMine`, Gebühr, `createTransaction`, signieren und senden (Umsetzung in `src/nodewallet.cpp`) |
| `ui_interface.h` | ohne Wirkung: keine GUI |
| `rpc/server.h`, `rpc/server.cpp` | Cores Befehlstabelle, Anfrage-Parsing, `help`/`stop`/`uptime`/`getrpcinfo` (Code aus Cores `rpc/server.cpp` ohne Timer und Signale); jeder Befehl läuft unter `cs_main` |
| `index/txindex.h` | `g_txindex`: der Index ist der des Nodes, also immer synchron |
| `wallet/rpcwallet.h`, `random.h` | leer bzw. `GetRandBytes` für das Cookie |
| `clientversion.h`, `chainparamsbase.h`, `shutdown.h`, `init.h`, `core_io.h` | Konstanten und Kleinigkeiten; `core_io.h` mit `DecodeHexTx`/`EncodeHexTx` aus Core |

Die Kette füllt der Indexer (`src/indexer.cpp`) aus den verarbeiteten Blöcken. `ReadBlockFromDisk`
und `GetTransaction` fragen die Node per RPC; schlägt das mitten in einem Block fehl, bricht das
Programm ab, statt OmniLite eine fehlende Transaktion als „nicht vorhanden“ zu melden. Beim
Neustart lädt OmniLite dann seinen letzten gesicherten State.
