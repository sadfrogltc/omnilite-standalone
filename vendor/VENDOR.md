# Übernommener Code

`vendor/core` enthält Dateien aus dem OmniLite-Repository (Litecoin Core 0.18 mit Omni Layer),
`github.com/OmniLite/omnilite`, Commit in `SOURCE_COMMIT`. Die Pfade unter `vendor/core` entsprechen
denen unter `src/` im Original. Lizenz: MIT, siehe `core/COPYING`, `core/univalue/COPYING` und
`core/secp256k1/COPYING`.

## Dateien

- Grundlagen: `amount.h`, `attributes.h`, `tinyformat.h`, `prevector.h`, `span.h`, `serialize.h`,
  `streams.h`, `version.h`, `uint256`, `arith_uint256`, `compat/endian.h`, `compat/byteswap.h`,
  `support/cleanse`, `support/allocators/zeroafterfree.h`, `util/strencodings`
- Hashes: `hash`, `crypto/common.h`, `crypto/sha256`, `crypto/sha512`, `crypto/hmac_sha512`,
  `crypto/ripemd160`
- Transaktionen, Script, Adressen: `primitives/transaction`, `script/script`, `script/standard`,
  `script/interpreter.h`, `script/script_error.h`, `pubkey`, `base58`, `bech32`, `key_io`
- Gebühren: `policy/feerate` (für die Dust-Grenze)
- RPC: `rpc/protocol` (Fehlercodes, Antwortformat), `rpc/util` (Hilfetexte von `RPCHelpMan`)
- Bibliotheken: `univalue/` (include, lib), `secp256k1/` (include, src)

Die Omni-Teile aus OmniLite liegen in `src/omnicore`; ihre Herkunft steht in `src/omnicore/ORIGIN.md`.

`chainparams.h` kommt nicht aus Core: `src/chainparams.h` ersetzt es mit den Werten aus
`src/chainparams.cpp` des Originals.

## Änderungen am Original

Jede Änderung ist im Code mit `omnilite-standalone:` markiert.

1. `key_io.h`, `key_io.cpp`: `#include <key.h>` und die Funktionen für private und erweiterte Schlüssel
   (`DecodeSecret`, `EncodeSecret`, `DecodeExtKey`, `EncodeExtKey`, `DecodeExtPubKey`,
   `EncodeExtPubKey`) entfernt. Schlüssel verwaltet die Wallet des Nodes.
2. `script/standard.cpp`: `#include <util/system.h>` entfernt; die Datei braucht nichts daraus.
3. `rpc/util.h`, `rpc/util.cpp`: `AddrToPubKey`, `AddAndGetMultisigDestination`, `ParseConfirmTarget`,
   `RPCErrorFromTransactionError`, `JSONRPCTransactionError` und `g_rpc_interfaces` entfernt (brauchen Keystore,
   Gebührenschätzung oder Node-Schnittstellen).

## Build-Einstellungen

- Core-Dateien: `HAVE_ENDIAN_H`, `HAVE_BYTESWAP_H`, `HAVE_DECL_BSWAP_16/32/64` (was `configure` unter
  glibc ermittelt). Kein `USE_ASM`: SHA256 läuft in der portablen Fassung.
- libsecp256k1: wie Cores Build auf 64-Bit-Linux (`USE_FIELD_5X52`, `USE_SCALAR_4X64`,
  `HAVE___INT128`, `USE_NUM_NONE`, eingebaute Inversion), dazu `ENABLE_MODULE_RECOVERY`.
