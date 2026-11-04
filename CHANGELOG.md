# Änderungen

## 0.1.0

Erste Fassung zum Testen auf einem echten Server.

- Indexer: folgt der Chain per RPC ab dem Omni-Genesis-Block, jeder Block läuft durch OmniLite 0.18 selbst,
  mit Reorgs und Sicherung des Stands.
- API: die `omni_*`-Aufrufe wie bei `omnilitecored`, unbestätigte Transaktionen eingeschlossen, dazu
  `omnilite_getstatus`.
- Senden über die Wallet der Node, mit Probelauf vor jeder Transaktion; DEx-Zahlung nur auf ausdrücklichen
  Aufruf und mit Wächter.
- Snapshot-Export und -Import, systemd-Unit.

Geprüft: auf Regtest gegen `omnilited` v0.18.2 (331 Vergleiche, keine Abweichung). Noch nicht geprüft: Mainnet,
ein aktueller `litecoind`, ein echter Dienstbetrieb unter systemd, DEx-Zahlungen gegen eine echte Node.
