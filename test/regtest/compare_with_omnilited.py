#!/usr/bin/env python3
# Copyright (c) 2026 The OmniLite Standalone developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compares omnilite-standalone with OmniLite's own omnilited on regtest.

omnilited runs as the node; omnilite-standalone indexes from it. A scenario
sends Omni transactions of all kinds, some through omnilited's RPCs, some
through omnilite-standalone's API (which builds them itself and lets
omnilited's wallet sign). After every block both are asked the same omni_*
calls; every difference is reported. At the end: a reorg, a restart of
omnilite-standalone and a move through a snapshot.

Usage:
  compare_with_omnilited.py --omnilited <path> --standalone <path> [--keep]

Exit code 0 if no difference was found.
"""

import argparse
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

NODE_RPC_PORT = 29443
NODE_P2P_PORT = 29444
API_PORT = 29543
USER, PASSWORD = "compare", "compare-pw"


class RPCError(Exception):
    def __init__(self, code, message):
        super().__init__(f"{code}: {message}")
        self.code = code


class Client:
    def __init__(self, name, port):
        self.name = name
        self.port = port

    def __call__(self, method, *params):
        body = json.dumps({"jsonrpc": "1.0", "id": 1, "method": method, "params": list(params)}).encode()
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/", body, {"Content-Type": "application/json"})
        req.add_header("Authorization", "Basic " + base64.b64encode(f"{USER}:{PASSWORD}".encode()).decode())
        try:
            with urllib.request.urlopen(req, timeout=300) as resp:
                data = json.load(resp)
        except urllib.error.HTTPError as e:
            data = json.load(e)
        if data.get("error"):
            raise RPCError(data["error"]["code"], data["error"]["message"])
        return data["result"]


def wait_for(what, check, timeout=120):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            if check():
                return
        except (RPCError, OSError, urllib.error.URLError) as e:
            last = e
        time.sleep(0.25)
    raise RuntimeError(f"timeout waiting for {what} ({last})")


def diff(a, b, path=""):
    """Differences between two JSON values, as readable lines."""
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append(f"{path}.{k}: only in standalone: {json.dumps(b[k])[:200]}")
            elif k not in b:
                out.append(f"{path}.{k}: only in omnilited: {json.dumps(a[k])[:200]}")
            else:
                out += diff(a[k], b[k], f"{path}.{k}")
        return out
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return [f"{path}: {len(a)} entries in omnilited, {len(b)} in standalone"]
        out = []
        for i, (x, y) in enumerate(zip(a, b)):
            out += diff(x, y, f"{path}[{i}]")
        return out
    if a != b:
        return [f"{path}: omnilited {json.dumps(a)[:200]} != standalone {json.dumps(b)[:200]}"]
    return []


# Lists OmniLite builds from an unordered_map: their order is not defined, also not in omnilited.
UNORDERED = {"omni_getallbalancesforid", "omni_getallbalancesforaddress", "omni_getwalletaddressbalances",
             "omni_getwalletbalances", "omni_getactivedexsells", "omni_getactivecrowdsales"}


def sort_list(value):
    if not isinstance(value, list):
        return value
    return sorted(value, key=lambda v: json.dumps(v, sort_keys=True))


class Comparison:
    def __init__(self, args):
        self.args = args
        os.makedirs(args.workdir, exist_ok=True)
        self.dir = tempfile.mkdtemp(prefix="run-", dir=args.workdir)
        self.node = Client("omnilited", NODE_RPC_PORT)
        self.api = Client("standalone", API_PORT)
        self.node_proc = None
        self.api_proc = None
        self.standalone_dir = os.path.join(self.dir, "standalone")
        self.differences = []
        self.checks = 0
        self.txids = []

    # --- processes ---

    def start_node(self):
        datadir = os.path.join(self.dir, "omnilited")
        os.makedirs(datadir, exist_ok=True)
        self.node_proc = subprocess.Popen(
            [self.args.omnilited, "-regtest", f"-datadir={datadir}", "-server", "-txindex", "-listen=0",
             f"-port={NODE_P2P_PORT}", f"-rpcport={NODE_RPC_PORT}", f"-rpcuser={USER}", f"-rpcpassword={PASSWORD}",
             "-fallbackfee=0.0002", "-printtoconsole=0", "-omnidebug=tokens"],
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        wait_for("omnilited", lambda: self.node("getblockcount") >= 0)

    def start_standalone(self, datadir=None):
        datadir = datadir or self.standalone_dir
        os.makedirs(datadir, exist_ok=True)
        log = open(os.path.join(self.dir, "standalone.log"), "a")
        self.api_proc = subprocess.Popen(
            [self.args.standalone] + self.standalone_args(datadir),
            stdout=log, stderr=subprocess.STDOUT)
        wait_for("omnilite-standalone", lambda: not self.api("omnilite_getstatus")["loading"])

    def standalone_args(self, datadir):
        return [f"-datadir={datadir}", "-network=regtest", f"-rpcport={NODE_RPC_PORT}", f"-rpcuser={USER}",
                f"-rpcpassword={PASSWORD}", f"-apiport={API_PORT}", f"-apiuser={USER}", f"-apipassword={PASSWORD}",
                "-pollinterval=1"]

    def stop_standalone(self):
        if self.api_proc:
            self.api_proc.terminate()
            self.api_proc.wait(timeout=60)
            self.api_proc = None

    def stop(self):
        self.stop_standalone()
        if self.node_proc:
            try:
                self.node("stop")
            except Exception:
                self.node_proc.terminate()
            self.node_proc.wait(timeout=120)

    # --- chain ---

    def mine(self, n=1):
        self.node("generatetoaddress", n, self.miner)
        height = self.node("getblockcount")
        wait_for("standalone at the node's height", lambda: self.api("omnilite_getstatus")["block"] == height
                 and self.api("omnilite_getstatus")["synced"])
        self.compare_blocks(height - n + 1, height)

    def fund(self, amounts):
        """Pays from the miner's coins only: the wallet would otherwise spend the senders' outputs."""
        coins = [u for u in self.node("listunspent", 1, 9999999, [self.miner]) if u["amount"] >= 49]
        need = sum(amounts.values()) + 0.001
        inputs, total = [], 0
        for u in coins:
            inputs.append({"txid": u["txid"], "vout": u["vout"]})
            total += u["amount"]
            if total >= need:
                break
        outputs = dict(amounts)
        outputs[self.miner] = round(total - need, 8)
        raw = self.node("createrawtransaction", inputs, outputs)
        signed = self.node("signrawtransactionwithwallet", raw)["hex"]
        self.node("sendrawtransaction", signed)

    def send(self, via, method, *params):
        client = self.node if via == "node" else self.api
        try:
            txid = client(method, *params)
        except RPCError as e:
            raise RuntimeError(f"{client.name} {method}{params}: {e}") from e
        self.txids.append(txid)
        return txid

    # --- comparing ---

    def same(self, what, method, *params):
        self.checks += 1
        try:
            a = self.node(method, *params)
        except RPCError as e:
            a = {"error": e.code}
        try:
            b = self.api(method, *params)
        except RPCError as e:
            b = {"error": e.code}
        if method in UNORDERED:
            a, b = sort_list(a), sort_list(b)
        lines = diff(a, b)
        if lines:
            self.differences.append((what, lines))
            print(f"  DIFF {what}:")
            for line in lines[:20]:
                print(f"    {line}")
        return a

    def compare_blocks(self, first, last):
        for h in range(first, last + 1):
            txids = self.same(f"block {h} transactions", "omni_listblocktransactions", h)
            for txid in txids if isinstance(txids, list) else []:
                before = len(self.differences)
                self.same(f"transaction {txid}", "omni_gettransaction", txid)
                if len(self.differences) > before and self.args.debug:
                    raw = self.api("getrawtransaction", txid, True)
                    time.sleep(2)
                    later = self.api("omni_gettransaction", txid)
                    print(f"    debug: node blockhash {raw.get('blockhash')}, standalone 2s later: "
                          f"block {later.get('block')} confirmations {later.get('confirmations')}")
        # Only the tip's hash can be asked for.
        self.same(f"consensus hash at {last}", "omni_getcurrentconsensushash")

    def compare_state(self, label):
        print(f"- state: {label}")
        info_node, info_api = self.node("omni_getinfo"), self.api("omni_getinfo")
        keys = ["block", "blocktime", "blocktransactions", "totaltrades", "totaltransactions", "alerts"]
        lines = diff({k: info_node.get(k) for k in keys}, {k: info_api.get(k) for k in keys})
        if lines:
            self.differences.append(("omni_getinfo", lines))
            print("  DIFF omni_getinfo:", lines)
        self.same("consensus hash", "omni_getcurrentconsensushash")
        properties = self.same("properties", "omni_listproperties")
        for p in properties:
            pid = p["propertyid"]
            # Calls that do not fit the property fail in both, with the same code.
            self.same(f"property {pid}", "omni_getproperty", pid)
            self.same(f"balances of {pid}", "omni_getallbalancesforid", pid)
            self.same(f"balances hash of {pid}", "omni_getbalanceshash", pid)
            self.same(f"grants of {pid}", "omni_getgrants", pid)
            self.same(f"crowdsale {pid}", "omni_getcrowdsale", pid, True)
            self.same(f"NFT ranges of {pid}", "omni_getnonfungibletokenranges", pid)
            self.same(f"NFT data of {pid}", "omni_getnonfungibletokendata", pid)
        for addr in self.addresses.values():
            self.same(f"balances of {addr}", "omni_getallbalancesforaddress", addr)
        self.same("active DEx offers", "omni_getactivedexsells")
        self.same("active crowdsales", "omni_getactivecrowdsales")
        self.same("wallet transactions", "omni_listtransactions", "*", 1000)
        self.same("wallet balances", "omni_getwalletbalances")
        self.same("wallet address balances", "omni_getwalletaddressbalances")

    # --- scenario ---

    def run(self):
        print(f"working directory: {self.dir}")
        self.start_node()
        self.miner = self.node("getnewaddress", "", "legacy")
        self.addresses = {
            "A": self.node("getnewaddress", "", "legacy"),
            "B": self.node("getnewaddress", "", "legacy"),
            "C": self.node("getnewaddress", "", "p2sh-segwit"),
            "D": self.node("getnewaddress", "", "legacy"),
        }
        A, B, C, D = (self.addresses[k] for k in "ABCD")
        self.node("generatetoaddress", 101, self.miner)
        self.fund({A: 10, B: 10, C: 10})
        self.node("generatetoaddress", 1, self.miner)
        self.start_standalone()
        self.mine()

        print("- issuances and sends")
        self.send("node", "omni_sendissuancefixed", A, 1, 2, 0, "Games", "Cards", "Fixed", "example.com", "data", "1000")
        self.send("api", "omni_sendissuancefixed", B, 2, 1, 0, "Games", "", "Test eco", "", "", "500")
        self.mine()
        self.send("node", "omni_send", A, B, 3, "100")
        self.mine()
        # Pending transactions as both see them.
        self.send("api", "omni_send", A, C, 3, "50")
        wait_for("pending in standalone", lambda: len(self.api("omni_listpendingtransactions")) == 1, 30)
        self.same("pending transactions", "omni_listpendingtransactions")
        self.mine()
        self.send("node", "omni_send", C, D, 3, "10")  # from P2SH-segwit
        self.send("api", "omni_funded_send", A, B, 3, "1", C)
        self.mine()
        # The funded send gave all of A's coins to C as change.
        self.fund({A: 10})
        self.mine()

        print("- managed property, grants, freezing")
        self.send("node", "omni_sendissuancemanaged", A, 1, 2, 0, "Companies", "", "Managed", "", "")
        self.mine()
        self.send("api", "omni_sendgrant", A, B, 4, "200", "memo")
        self.send("node", "omni_sendgrant", A, "", 4, "300")
        self.mine()
        self.send("node", "omni_sendrevoke", A, 4, "50")
        self.send("node", "omni_sendenablefreezing", A, 4)
        self.mine()
        self.send("api", "omni_sendfreeze", A, B, 4, "1")
        self.mine()
        self.send("node", "omni_send", B, D, 4, "1")  # frozen: invalid in both
        self.mine()
        self.send("api", "omni_sendunfreeze", A, B, 4, "1")
        self.send("node", "omni_senddisablefreezing", A, 4)
        self.mine()

        print("- crowdsale")
        deadline = int(time.time()) + 10_000_000
        self.send("node", "omni_sendissuancecrowdsale", B, 1, 2, 0, "Companies", "", "Crowdsale", "", "", 3, "2", deadline, 10, 5)
        self.mine()
        self.send("api", "omni_send", A, B, 3, "20")  # takes part
        self.mine()
        self.send("node", "omni_sendclosecrowdsale", B, 5)
        self.mine()
        self.send("node", "omni_sendchangeissuer", A, D, 4)
        self.mine()

        print("- non-fungible tokens")
        self.send("node", "omni_sendissuancemanaged", A, 1, 5, 0, "Art", "", "Pictures", "", "")
        self.mine()
        self.send("node", "omni_sendgrant", A, A, 6, "10", "")
        self.mine()
        self.send("api", "omni_sendnonfungible", A, B, 6, 1, 3)
        self.mine()
        self.send("node", "omni_setnonfungibledata", 6, 1, 3, False, "holder data")
        self.send("api", "omni_setnonfungibledata", 6, 4, 10, True, "issuer data")
        self.mine()

        print("- send all, any data, invalid ones")
        self.send("api", "omni_sendall", B, D, 1)
        self.send("node", "omni_sendanydata", A, "deadbeef")
        self.mine()
        payload = self.node("omni_createpayload_simplesend", 3, "1000000")
        self.send("node", "omni_sendrawtx", C, payload, D)  # more than C has: invalid in both
        self.send("node", "omni_sendsto", A, 3, "10")  # no OMNI for the fee: invalid in both
        self.mine()
        self.compare_state("after the scenario")

        print("- reorg")
        tip = self.node("getblockcount")
        self.node("invalidateblock", self.node("getblockhash", tip - 2))
        # The transactions of the dropped blocks come back in the new ones.
        self.mine(4)
        # The wallet of omnilited counts the outputs of the dropped blocks as conflicted: fresh coins for D.
        self.fund({D: 1})
        self.mine()
        self.send("node", "omni_send", D, A, 3, "5")
        self.mine()
        self.compare_state("after the reorg")

        print("- restart of omnilite-standalone")
        self.stop_standalone()
        self.send("node", "omni_send", D, A, 3, "1")
        self.node("generatetoaddress", 1, self.miner)
        self.start_standalone()
        self.mine()
        self.compare_state("after the restart")

        print("- move through a snapshot")
        self.stop_standalone()
        snapshot = os.path.join(self.dir, "snapshot.tar")
        out = subprocess.run([self.args.standalone] + self.standalone_args(self.standalone_dir) +
                             [f"-exportsnapshot={snapshot}"], capture_output=True, text=True)
        if out.returncode != 0:
            raise RuntimeError("export failed: " + out.stdout + out.stderr)
        expected = self.node("omni_getcurrentconsensushash")["consensushash"]
        moved = os.path.join(self.dir, "standalone-moved")
        out = subprocess.run([self.args.standalone] + self.standalone_args(moved) +
                             [f"-importsnapshot={snapshot}", f"-snapshothash={expected}"], capture_output=True, text=True)
        if out.returncode != 0:
            raise RuntimeError("import failed: " + out.stdout + out.stderr)
        self.start_standalone(moved)
        self.send("node", "omni_send", D, A, 3, "1")
        self.mine()
        self.compare_state("after the snapshot move")

        print()
        print(f"{self.checks} comparisons, {len(self.txids)} transactions sent, {len(self.differences)} differences")
        for what, lines in self.differences:
            print(f"DIFF {what}")
            for line in lines:
                print(f"  {line}")
        return 0 if not self.differences else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--omnilited", required=True)
    parser.add_argument("--standalone", required=True)
    parser.add_argument("--keep", action="store_true", help="keep the working directory")
    parser.add_argument("--debug", action="store_true", help="look closer at differing transactions")
    parser.add_argument("--workdir", default=os.path.expanduser("~/omnilite-compare"), help="where the runs go (default: ~/omnilite-compare)")
    args = parser.parse_args()
    comparison = Comparison(args)
    try:
        rc = comparison.run()
    except Exception as e:
        print(f"ERROR: {e}")
        rc = 2
    finally:
        comparison.stop()
        if args.keep or rc:
            print(f"kept: {comparison.dir}")
        else:
            shutil.rmtree(comparison.dir, ignore_errors=True)
    sys.exit(rc)


if __name__ == "__main__":
    main()
