# Reporting Security Issues

We take security bugs in esp-miner seriously. We appreciate your efforts to responsibly disclose your findings, and will make every effort to acknowledge your contributions.

To report a security issue, please use the GitHub Security Advisory ["Report a Vulnerability"](https://github.com/bitaxeorg/ESP-Miner/security/advisories/new) tab.

An esp-miner maintainer will send a response indicating the next steps in handling your report. After the initial reply to your report, the security team will keep you informed of the progress towards a fix and full announcement, and may ask for additional information or guidance.

## Project Maintainers & Verification Keys

The following maintainers with merge permissions manage security reports and releases for esp-miner:

| Maintainer | GitHub | Verification Keys (GPG / SSH) |
|------------|--------|-------------------------------|
| Skot | [@skot](https://github.com/skot) | SSH: [`skot.keys`](https://github.com/skot.keys) |
| mutatrum | [@mutatrum](https://github.com/mutatrum) | SSH: [`mutatrum.keys`](https://github.com/mutatrum.keys) |
| WantClue | [@WantClue](https://github.com/WantClue) | SSH: [`WantClue.keys`](https://github.com/WantClue.keys) |
| johnny9 | [@johnny9](https://github.com/johnny9) | SSH: [`johnny9.keys`](https://github.com/johnny9.keys) |
| Benjamin Wilson | [@wilsob12](https://github.com/wilsob12) | SSH: [`wilsob12.keys`](https://github.com/wilsob12.keys) |
| Erik Olof Gunnar Andersson | [@eandersson](https://github.com/eandersson) | GPG: `06BA 5E6E E8A3 21AD 2996 0228 199A 0FFE 5AAA 0452`<br>SSH: [`eandersson.keys`](https://github.com/eandersson.keys) |
| 0xf0xx0 | [@0xf0xx0](https://github.com/0xf0xx0) | GPG: [`0xf0xx0.gpg`](https://github.com/0xf0xx0.gpg) |

### Verifying Signatures

#### GPG Signatures
```bash
# Import GPG key from GitHub profile
curl -s https://github.com/<username>.gpg | gpg --import

# Or receive GPG key from public keyserver by fingerprint
gpg --keyserver hkps://keyserver.ubuntu.com --recv-keys "<fingerprint>"
```

#### SSH Signatures & Public Keys
```bash
# Fetch maintainer's public SSH key(s)
curl -s https://github.com/<username>.keys
```

Report security bugs in third-party modules to the person or team maintaining the module.
