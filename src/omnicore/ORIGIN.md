# Herkunft von src/omnicore

Die Dateien hier stammen aus OmniLite 0.18.1 (`github.com/OmniLite/omnilite`, `src/omnicore/`,
Commit `fbc27c1`, MIT). Anders als `vendor/core` sind sie stärker angepasst, weil das Original einen
vollen Litecoin-Core-Node voraussetzt. Jede Änderung ist im Code mit `omnilite-standalone:` markiert.

| Datei | Herkunft | Änderungen |
|---|---|---|
| `parsing.h/.cpp` | unverändert | `parsing.h`: `<stdint.h>` ergänzt |
| `script.h/.cpp` | unverändert bis auf Dust | `GetDustThreshold` aus Cores `policy/policy.cpp` hineinkopiert, `minRelayTxFee` hier definiert (1000 litoshi/kB wie Litecoins `DEFAULT_MIN_RELAY_TX_FEE`) |
| `encoding.h/.cpp` | unverändert bis auf Zufall | `GetRand(256)` → `std::random_device` (nur für die Wahl des letzten Bytes der Class-B-Schlüssel, nicht konsensrelevant) |
| `createpayload.h/.cpp` | unverändert | – |
| `rules.h` | unverändert | – |
| `rules.cpp` | Anfang bis `IsAllowedOutputType` | Activations, Feature-Prüfungen und Checkpoints folgen in Phase 3 |
| `tx.h` | unverändert | – |
| `tx.cpp` | Anfang bis vor `interpretPacket` | `interpretPacket` und `logicMath_*` folgen in Phase 3 |
| `omnicore.h` | Auszug | nur Konstanten und Deklarationen, die die Dateien hier brauchen; dazu die Schnittstelle `InputSource` |
| `omnicore.cpp` | Auszug | `GetEncodingClass`, `parseTransaction`, `ExodusAddress`, `GetOmMarker`, Formatierer und Ecosystem-Prüfungen wörtlich; der Input-Cache (`FillTxInputCache`, `view`, Locks) ist durch `InputSource` ersetzt; `exodus_address` folgt dem gewählten Netz statt in `mastercore_init` gesetzt zu werden; `ScriptToAsmStr` gibt nur Hex aus (nur Debug-Log); `isPropertyDivisible` ist bis Phase 3 ein Platzhalter (nur Debug-Log) |
| `utilsbitcoin.h/.cpp` | Auszug | nur `MainNet`, `TestNet`, `RegTest`, `isNonMainNet` |
| `log.h` | ersetzt | gleiche Flags und `PrintToLog`, Ausgabe ins Programm-Log auf Debug-Ebene |

## Tests

`test/omnicore/` enthält die Decoder-Tests aus `src/omnicore/test/` (`parsing_a/b/c`, `sender_bycontribution`,
`sender_firstin`, `marker`, `exodus`, `obfuscation`, `output_restriction`, `script_extraction`,
`script_solver`, `utils_tx`). Sie liefen in OmniLite selbst nicht: Die erwarteten Adressen stammen aus
Omni Core (Bitcoin, Versionsbytes 0, 5 und ein nicht passendes 14) und die Exodus-Tests erwarten die
Bitcoin-Exodus-Adressen. Für Litecoin wurden die Adressen mechanisch umgerechnet (gleicher Hash,
Versionsbyte 48 für P2PKH, 50 für P2SH) und die Exodus-Adressen durch die von OmniLite ersetzt.
`test_shim.h/.cpp` stellt bereit, was die Tests aus Cores Test-Umgebung erwarten (`BasicTestingSetup`,
`Coin`, `view`, `GetRandInt`). `std::random_shuffle` ist durch `std::shuffle` ersetzt.
