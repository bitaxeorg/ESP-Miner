# Reporting Security Issues

We take security bugs in esp-miner seriously. We appreciate your efforts to responsibly disclose your findings, and will make every effort to acknowledge your contributions.

To report a security issue, please use the GitHub Security Advisory ["Report a Vulnerability"](https://github.com/bitaxeorg/ESP-Miner/security/advisories/new) tab.

An esp-miner maintainer will send a response indicating the next steps in handling your report. After the initial reply to your report, the security team will keep you informed of the progress towards a fix and full announcement, and may ask for additional information or guidance.

## Project Maintainers & Verification Keys

The following maintainers with merge permissions manage security reports and releases for esp-miner:

| Maintainer | email | SSH | GPG |
|------------|-------|-----|-----|
| [skot](https://github.com/skot) | | [`skot.keys`](https://github.com/skot.keys) | |
| [Johnny](https://github.com/johnny9) | | [`johnny9.keys`](https://github.com/johnny9.keys) | |
| [Benjamin Wilson](https://github.com/benjamin-wilson) | | | |
| [WantClue](https://github.com/WantClue) | | [`WantClue.keys`](https://github.com/WantClue.keys) | |
| [mutatrum](https://github.com/mutatrum) | [mutatrum@gmail.com](mutatrum@gmail.com) | [`mutatrum.keys`](https://github.com/mutatrum.keys) | |
| [Erik Olof Gunnar Andersson](https://github.com/eandersson) | | [`eandersson.keys`](https://github.com/eandersson.keys) | `06BA 5E6E E8A3 21AD 2996 0228 199A 0FFE 5AAA 0452` |
| [0xf0xx0](https://github.com/0xf0xx0) | | | [`0xf0xx0.gpg`](https://github.com/0xf0xx0.gpg) |

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
