#!/usr/bin/env python3
"""Check the pinned trust store and the published eMudhra incident chains offline."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
BUNDLE = ROOT / "certificates/cacert-2026-10-20.pem"
CHAINS = ROOT / "certificates/test/emudhra-chains-2026-10-20.zip"
CHAINS_SHA256 = "8a2edc81b6c51e3ad27d45ca1e441ec84c961e2dee0fc547f2a02c0f74315f95"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def verify(openssl, certificate, *options):
    return subprocess.run(
        [openssl, "verify", "-CAfile", str(BUNDLE), "-no-CApath", "-no-CAstore",
         "-no_check_time", *options, str(certificate)],
        capture_output=True, text=True, check=False,
    )


def check_build(build_dir, idf_path):
    require(idf_path, "--build-dir requires --idf-path or an exported IDF_PATH")
    config = json.loads((build_dir / "config/sdkconfig.json").read_text())
    for key in ("MBEDTLS_CERTIFICATE_BUNDLE", "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE",
                "MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE"):
        require(config.get(key) is True, f"Build must enable {key}")
    for key in ("MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", "MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN",
                "MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST"):
        require(not config.get(key), f"Build must disable {key}")
    require(config.get("MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH") == str(BUNDLE.relative_to(ROOT)),
            "Build must use the pinned CA file")
    generator = Path(idf_path) / "components/mbedtls/esp_crt_bundle/gen_crt_bundle.py"
    with tempfile.TemporaryDirectory() as temporary:
        subprocess.run([sys.executable, str(generator), "--input", str(BUNDLE)],
                       cwd=temporary, check=True)
        expected = (Path(temporary) / "x509_crt_bundle").read_bytes()
    actual = (build_dir / "esp-idf/mbedtls/x509_crt_bundle").read_bytes()
    require(actual == expected, "Generated trust store differs from the pinned CA bundle")
    require(actual in (build_dir / "esp-miner.bin").read_bytes(),
            "Firmware does not contain the expected trust store")
    print(f"Firmware contains exactly the pinned trust store ({len(actual)} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openssl", default="openssl", help="OpenSSL 3 executable")
    parser.add_argument("--build-dir", type=Path, help="Also check a completed ESP-IDF firmware build")
    parser.add_argument("--idf-path", default=os.environ.get("IDF_PATH"))
    args = parser.parse_args()

    expected_hash = BUNDLE.with_suffix(".pem.sha256").read_text().split()[0]
    require(hashlib.sha256(BUNDLE.read_bytes()).hexdigest() == expected_hash,
            "Pinned CA bundle checksum mismatch")
    require(hashlib.sha256(CHAINS.read_bytes()).hexdigest() == CHAINS_SHA256,
            "Incident test archive checksum mismatch")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cafile=str(BUNDLE))
    require(context.cert_store_stats()["x509_ca"] == 118, "Expected 118 trusted roots")
    names = {value for cert in context.get_ca_certs() for rdn in cert["subject"]
             for key, value in rdn if key == "commonName"}
    require(not names.intersection({"emSign ECC Root CA - G3", "AAA Certificate Services"}),
            "An affected trust anchor is still present")

    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        positive = re.search(r"^ISRG Root X1\n=+\n.*?(-----BEGIN CERTIFICATE-----.*?"
                             r"-----END CERTIFICATE-----\n)", BUNDLE.read_text(), re.M | re.S)
        require(positive, "Missing positive control: ISRG Root X1")
        certificate = temporary / "isrg-root-x1.pem"
        certificate.write_text(positive.group(1))
        result = verify(args.openssl, certificate)
        require(result.returncode == 0, f"Positive control failed: {result.stdout}{result.stderr}")
        with zipfile.ZipFile(CHAINS) as archive:
            chains = [name for name in archive.namelist() if name.endswith(".pem")]
            require(len(chains) == 12, "Expected all 12 incident chains")
            for name in chains:
                certificate = temporary / Path(name).name
                certificate.write_bytes(archive.read(name))
                result = verify(args.openssl, certificate, "-purpose", "sslserver",
                                "-verify_hostname", certificate.stem,
                                "-untrusted", str(certificate))
                require(result.returncode == 2 and "error 20 at" in result.stderr,
                        f"Expected an untrusted issuer for {name}: {result.stdout}{result.stderr}")
                print(f"Rejected untrusted issuer: {certificate.stem}")
    print("118 pinned roots verified; positive control accepted; all 12 incident chains rejected")
    if args.build_dir:
        check_build(args.build_dir.resolve(), args.idf_path)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
