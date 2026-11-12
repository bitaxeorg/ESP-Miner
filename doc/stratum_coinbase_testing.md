# Testing large coinbase jobs on a miner

Run the local Stratum V1 test pool with Python 3.10 or newer; no packages are required:

```sh
python3 tools/stratum_coinbase_test.py
```

In AxeOS, set the active pool to your computer's LAN IP, port **3333**, protocol **Stratum V1**, and **TLS disabled**. Any username and password work (for example, `stress-test.worker` / `x`). The miner connects to the server; allow incoming TCP connections to port 3333 if your computer's firewall prompts. Keep the computer awake during the test.

This serves synthetic work with distinct Taproot payout scripts. It does not connect to Bitcoin, produce real payouts, or submit blocks. Restore your normal pool settings afterward. The listener defaults to all IPv4 interfaces for LAN access; use `--host 127.0.0.1` for local software tests.

The default sequence repeats **110 → 1,800 → 10,000 → 110 payouts**, holding each job for 60 seconds at share difficulty 128. Each new job has `clean_jobs=true`. The server validates submitted shares by reconstructing the complete coinbase and block header, including extranonces and negotiated version bits. Miner difficulty suggestions are acknowledged while the configured test difficulty stays fixed.

Look for these log entries:

```text
JOB job=00000003 outputs=10000 coinb2=430011 bytes line=... bytes difficulty=128.0
ACCEPT job=00000003 outputs=10000 verified full coinbase
STAGE job=00000003 outputs=10000 accepted=... rejected=... PASS
```

`PASS` means at least one share met the test difficulty for that specific job. Check AxeOS hashrate and the accepted-share rate as well to assess sustained performance. `NO_SHARES` means that stage received no valid work; it is not a diagnosis by itself. `CONNECT` and `DISCONNECT` entries expose reconnects. A passing recovery test includes verified shares at 10,000 payouts and again at 110 payouts **on the same connection**. Shares for the previous job get a five-second transition grace period and are logged under their original job ID.

To hold 10,000 payouts for a longer soak test:

```sh
python3 tools/stratum_coinbase_test.py --counts 10000 --interval 600
```

To exercise fragmented delivery with 733-byte writes and a one-millisecond pause between writes:

```sh
python3 tools/stratum_coinbase_test.py --chunk-size 733 --chunk-delay 0.001
```

TCP may combine or split writes, so these are application write sizes rather than guaranteed packet sizes. Replies and notifications are serialized to keep JSON lines intact.

To test an oversized job and reconnect recovery:

```sh
python3 tools/stratum_coinbase_test.py --counts 110,20000,110 --interval 60
```

20,000 payouts produce an approximately 1.72 MB JSON line, above the fix's 1 MiB + 8 KiB limit. Expect an explicit oversized-line error in the miner log and a disconnect. Every new connection restarts the sequence at 110 payouts, so verify that shares resume after reconnect. Unfixed firmware can also disconnect or stall on the 1,800- and 10,000-payout stages; compare its logs with the fixed build.

Change `--difficulty` to adjust share frequency, `--interval` for the time per stage, and `--port` if 3333 is occupied. Higher difficulties produce fewer shares; choose an interval long enough to observe several shares from your hardware.

Run the tool's transaction, share-validation, and real TCP integration tests with:

```sh
python3 -m unittest discover -s tools -p 'test_stratum_coinbase_test.py' -v
```
