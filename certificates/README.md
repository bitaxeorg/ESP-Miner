# Embedded system CA bundle

ESP-Miner pins curl's Mozilla-derived bundle from October 20, 2026 (118 roots):

- Source: https://curl.se/ca/cacert-2026-10-20.pem
- Published checksum: https://curl.se/ca/cacert-2026-10-20.pem.sha256
- SHA-256: `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe`
- License: [Mozilla Public License 2.0](https://www.mozilla.org/MPL/2.0/), as documented by [curl](https://curl.se/docs/caextract.html).

The [eMudhra incident](https://emudhra.we-have-a-problem.com/) compromised intermediate CA keys. This snapshot removes `emSign ECC Root CA - G3` and excludes `AAA Certificate Services`, blocking the eMudhra and Sectigo cross-signed paths. Root removal also prevents connections to legitimate servers whose only trust path uses those roots. Pool operators must serve a chain to a remaining trusted root.

`sdkconfig.defaults` disables ESP-IDF's full, common, and deprecated lists and enables only this custom bundle. ESP-IDF custom bundles are otherwise **additive**. Do not re-enable the default or deprecated lists: ESP-IDF v6.0.2 includes affected roots in its bundled certificates. CMake rejects configurations that would bypass these settings, including existing `sdkconfig` files that override the defaults.

For an existing checkout, edit the seven certificate-bundle entries in `sdkconfig` to match `sdkconfig.defaults`. Alternatively, back up `sdkconfig`, remove it, and let `idf.py reconfigure` regenerate it from `sdkconfig.defaults` (reapply any other local settings). The build guard also runs during configuration, so update the file before running `idf.py menuconfig`. Use a clean build when changing trust stores.

The bundle is embedded in `esp-miner.bin`; deployed miners need a firmware OTA update. Updating the build host's trust store does not update devices. Stratum's **TLS (System certificate)** mode uses this bundle. **TLS (Custom certificate)** continues to use the configured certificate and needs separate review if that certificate trusts an affected chain.

## Verification

Run the offline regression check with Python 3 and OpenSSL 3:

```sh
python3 tools/test_ca_bundle.py
```

It checks the bundle checksum, the root count, absence of the two affected trust anchors, an accepted ISRG Root X1 control, and rejection of all 12 published incident chains for an untrusted issuer. It disables certificate time checks so that expired fixtures cannot make this security check pass accidentally (the firmware's default mbedTLS configuration also disables expiry checks). OpenSSL's system trust directories and store are excluded.

The test archive is an unmodified copy of https://emudhra.we-have-a-problem.com/chains.zip captured on October 20, 2026. Its SHA-256 is `8a2edc81b6c51e3ad27d45ca1e441ec84c961e2dee0fc547f2a02c0f74315f95`. Its embedded README describes the chains. It contains public certificates, no private keys. All 12 chains were accepted by ESP-IDF v6.0.2's original `cacrt_all.pem` plus `cacrt_local.pem`; the replacement rejects them.

After building with the ESP-IDF environment sourced, also verify that the generated bundle matches a fresh conversion and is embedded in the firmware:

```sh
python tools/test_ca_bundle.py --build-dir build
```

Host verification does not replace an on-device TLS handshake test. Before release, test both primary and fallback pools, confirm a connection to an unaffected pool in system-certificate mode, and confirm TLS rejection of the incident test hosts (including `dv-g3b-emudhra.we-have-a-problem.com` and `dv-g3b-aaa-emudhra.we-have-a-problem.com`). Review any configured custom CA separately.

For future bundle updates, download a dated snapshot and its published checksum, verify them, update the filename and settings in `sdkconfig.defaults` and `CMakeLists.txt`, update the checksum and expected root count in the test, and rerun the checks before building and distributing firmware. Builds never download a mutable CA bundle.
