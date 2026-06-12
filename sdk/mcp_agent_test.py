#!/usr/bin/env python3
"""Clean-room agent test: drive Aether to search BKK->LA flights using ONLY the
MCP server's tools (the recommended interface), exactly as a fresh agent would.

No hardcoded token, no hardcoded tab id, no direct gateway calls — everything
goes through aether_mcp.py over stdio JSON-RPC, which auto-discovers the running
browser via ~/.aether/gateway.json.
"""
import json
import re
import subprocess
import sys
import time

MCP = ["python3", __file__.replace("mcp_agent_test.py", "aether_mcp.py")]


class MCP:
    def __init__(self):
        self.p = subprocess.Popen(
            ["python3", sys.argv[0].replace("mcp_agent_test.py", "aether_mcp.py")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self._id = 0
        self._rpc("initialize", {})
        self._notify("notifications/initialized")

    def _send(self, obj):
        self.p.stdin.write(json.dumps(obj) + "\n"); self.p.stdin.flush()

    def _notify(self, method):
        self._send({"jsonrpc": "2.0", "method": method})

    def _rpc(self, method, params):
        self._id += 1
        self._send({"jsonrpc": "2.0", "id": self._id,
                    "method": method, "params": params})
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("MCP server closed")
            msg = json.loads(line)
            if msg.get("id") == self._id:
                return msg

    def tool(self, name, **args):
        r = self._rpc("tools/call", {"name": name, "arguments": args})
        res = r.get("result", {})
        c = res.get("content", [{}])[0]
        return c.get("text", c)


def main():
    m = MCP()
    print("connected to Aether via MCP (auto-discovered, no token handling)\n")

    m.tool("aether_navigate", url="https://www.google.com/travel/flights?hl=en&curr=USD")
    time.sleep(2)

    def fill(field, query):
        els = json.loads(m.tool("aether_observe"))
        ref = next(e["ref"] for e in els if field in e["name"])
        m.tool("aether_type", ref=ref, text=query)
        time.sleep(1.5)
        # pick the first suggestion that matches the query (observe -> click)
        key = query.split()[0].lower()
        opts = json.loads(m.tool("aether_observe", role="option", limit=30))
        hit = next((o for o in opts if key in o["name"].lower()), None)
        assert hit, f"no suggestion for {query}"
        m.tool("aether_click", ref=hit["ref"])
        print(f"  filled {field!r} = {query}  (picked: {hit['name'][:30]})")
        time.sleep(0.9)

    fill("Where from?", "Bangkok")
    fill("Where to?", "Los Angeles")

    # dates: Google auto-opens the calendar after destination is picked, so
    # check for day cells first; only click "Departure" if it isn't open yet.
    def daycells():
        return [e for e in json.loads(m.tool("aether_observe", role="gridcell",
                                             limit=400)) if e["name"]]
    cells = daycells()
    if not cells:
        els = json.loads(m.tool("aether_observe"))
        dep = next(e["ref"] for e in els if e["name"] == "Departure")
        m.tool("aether_click", ref=dep); time.sleep(1.2)
        cells = daycells()
    assert cells, "calendar did not open"
    m.tool("aether_click", ref=cells[14]["ref"]); time.sleep(0.5)
    cells = daycells()  # re-observe; refs shift after first pick
    m.tool("aether_click", ref=cells[35]["ref"]); time.sleep(0.5)
    print(f"  picked dates (departure + return)")

    # Done + Search
    els = json.loads(m.tool("aether_observe"))
    done = next((e["ref"] for e in els
                 if e["name"].startswith("Done. Search")), None)
    if done is not None:
        m.tool("aether_click", ref=done); time.sleep(2)
    els = json.loads(m.tool("aether_observe"))
    srch = next(e["ref"] for e in els if e["name"].strip() == "Search")
    m.tool("aether_click", ref=srch)
    print("  clicked Search")

    # wait + read results
    for _ in range(30):
        if "departing flights" in m.tool("aether_read_text").lower():
            break
        time.sleep(0.5)
    txt = m.tool("aether_read_text")
    prices = re.findall(r"\$[\d,]+", txt)
    print("\nRESULTS — Bangkok to Los Angeles:")
    for ln in txt.splitlines():
        if "$" in ln and re.search(r"\d", ln):
            print("   ", ln.strip()[:90])
    print("\nprices seen:", sorted(set(prices))[:8])
    print("OK" if prices else "NO RESULTS")


if __name__ == "__main__":
    main()
