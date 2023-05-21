import hashlib
import binascii


"""
The following is the reference stratum notify string as well as the bytes/values for the resulting data that gets sent to a bm1397


 {"id":null,"method":"mining.notify","params":["1f9a56282c","bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000","01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03e60e0cfabe6d6d7595fc426909f3a63c563a88773a618ec42cc51188ed0632b69f1c3053a8f8180100000000000000","2c28569a1f2f736c7573682f0000000003a1f3a22b000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a799d4c611eff5765ba06d2c58ad71b5734d677cea10942664b2a712d005108ae0000000000000000266a24aa21a9ed5c4d2056e3eef09b05d95897adec38c5c3f460a919e95f87e15664957c70305a00000000",
        ["4ea53a030256c37391b891b0d5060537df63944ce3fcd45121215596376bb3db","22cd1dde2c1b083237bbadd62ed1d51ee455265b7defe04dc8bcae7e5acacb33","60c781a8b02c07544cb3a91de3b4d7a13f9939c8579f3ac92fa28e802ace1b39",
        "d89820b36568adc0705d71d639e69ccb7c168a1051697846cf5d98e5725ee4e3","73f0f773a3b6097388984f934ba1b01afc771c33db6df126cd6971cfea9f8f49","420958bbb39f6b8ad30e5b45b38a3825bf76f619b7dbb73a0366605ff882e91d",
        "75f9ef87931104db956c88d65198596049af51017af4685c4548f2c31ec75b6d","70dd7189d5b927ac10a750062e5ab9f8b83fb784068e1c80d0df919bcf22e1b2","b34f2440b2b4609e44594885a397086339f4a2d880fb2d50ac585f757b895832",
        "4c62d861fb259a743d1e2787eeac5bdd22a9883b5cc0b025843cff9441ea6b74","62522d5d8e2ff9d721a9a4b91931ec61069fff7c8ad23119718c068a035b9b1b","a0e7cf5509d9d0d87ff9a4f6332f76a243de01f4e93289b290e937e7fd03224f"],
        "20000004","1705dd01","64658bd8",true]}

header: [20 00 00 04 BF 44 FD 35 13 DC 7B 83 7D 60 E5 C6 28 B5 72 B4 48 D2 04 A8 00 00 07 49 00 00 00 00 00 00 00 00 21 E8 1B CD 12 0D EF 32 CE CE 3D 05 7F 24 A0 1F D4 61 DB CF 32 EB D3 DB B4 F9 9E EA 46 A8 04 C6 64 65 8B D8 17 05 DD 01 00 00 00 00 00 00 00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00]
nonce1: 1165060344b679
nonce2: 0
nonce2_len: 8
ntime: 64658bd8
midstate: [91 DF EA 52 8A 9F 73 68 3D 0D 49 5D D6 DD 74 15 E1 CA 21 CB 41 17 59 E3 E0 5D 7D 5F F2 85 31 4D]
job cmd: [55 AA 21 96 04 04 00 00 00 00 01 DD 05 17 D8 8B 65 64 C6 04 A8 46 4D 31 85 F2 5F 7D 5D E0 E3 59 17 41 CB 21 CA E1 15 74 DD D6 5D 49 0D 3D 68 73 9F 8A 52 EA DF 91 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 6B 1D]
"""


def double_sha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def merkle_root(coinbase_tx_hash, merkle_branches):
    current_hash = bytes.fromhex(coinbase_tx_hash)
    for branch in merkle_branches:
        branch_hash = bytes.fromhex(branch)
        # Concatenate the hashes and compute the double SHA-256 hash
        current_hash = double_sha256(current_hash + branch_hash)
    return current_hash.hex()


def swap_endian_words(hex_words):
    '''Swaps the endianness of a hexadecimal string of words and returns as another hexadecimal string.'''

    message = binascii.unhexlify(hex_words)
    if len(message) % 4 != 0:
        raise ValueError('Must be 4-byte word aligned')
    swapped_bytes = [message[4 * i: 4 * i + 4][::-1] for i in range(0, len(message) // 4)]
    return ''.join([binascii.hexlify(word).decode('utf-8') for word in swapped_bytes])

coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03e60e0cfabe6d6d7595fc426909f3a63c563a88773a618ec42cc51188ed0632b69f1c3053a8f81801000000000000001165060344b67900000000000000002c28569a1f2f736c7573682f0000000003a1f3a22b000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a799d4c611eff5765ba06d2c58ad71b5734d677cea10942664b2a712d005108ae0000000000000000266a24aa21a9ed5c4d2056e3eef09b05d95897adec38c5c3f460a919e95f87e15664957c70305a00000000"
coinbase_tx_hash = double_sha256(bytes.fromhex(coinbase_tx)).hex()
merkle_branches = [
    "4ea53a030256c37391b891b0d5060537df63944ce3fcd45121215596376bb3db",
    "22cd1dde2c1b083237bbadd62ed1d51ee455265b7defe04dc8bcae7e5acacb33",
    "60c781a8b02c07544cb3a91de3b4d7a13f9939c8579f3ac92fa28e802ace1b39",
    "d89820b36568adc0705d71d639e69ccb7c168a1051697846cf5d98e5725ee4e3",
    "73f0f773a3b6097388984f934ba1b01afc771c33db6df126cd6971cfea9f8f49",
    "420958bbb39f6b8ad30e5b45b38a3825bf76f619b7dbb73a0366605ff882e91d",
    "75f9ef87931104db956c88d65198596049af51017af4685c4548f2c31ec75b6d",
    "70dd7189d5b927ac10a750062e5ab9f8b83fb784068e1c80d0df919bcf22e1b2",
    "b34f2440b2b4609e44594885a397086339f4a2d880fb2d50ac585f757b895832",
    "4c62d861fb259a743d1e2787eeac5bdd22a9883b5cc0b025843cff9441ea6b74",
    "62522d5d8e2ff9d721a9a4b91931ec61069fff7c8ad23119718c068a035b9b1b",
    "a0e7cf5509d9d0d87ff9a4f6332f76a243de01f4e93289b290e937e7fd03224f"
]


print("Merkle root:", merkle_root(coinbase_tx_hash, merkle_branches))
# cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846
# last 4 bytes of merkle is C6 04 A8 46

version = b'\x20\x00\x00\x04'  # little-endian encoding of version 1
prev_block_hash = bytes.fromhex(swap_endian_words("bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000"))
# merkle calculated above
merkle_root = bytes.fromhex(('cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846'))
timestamp = b'\x64\x65\x8b\xd8'  # little-endian encoding of Unix timestamp 64658bd8
difficulty_target = b'\x17\x05\xdd\x01' # little-endian encoding of difficulty 1705dd01
nonce = b'\x00\x00\x00\x00'  # example nonce value

block_header = version + prev_block_hash + merkle_root + timestamp + difficulty_target + nonce
print(block_header.hex())
#20000004bf44fd3513dc7b837d60e5c628b572b448d204a800000749000000000000000021e81bcd120def32cece3d057f24a01fd461dbcf32ebd3dbb4f99eea46a804c664658bd81705dd0100000000
#[20 00 00 04 BF 44 FD 35 13 DC 7B 83 7D 60 E5 C6 28 B5 72 B4 48 D2 04 A8 00 00 07 49 00 00 00 00 00 00 00 00 21 E8 1B CD 12 0D EF 32 CE CE 3D 05 7F 24 A0 1F D4 61 DB CF 32 EB D3 DB B4 F9 9E EA ]
# output: 0400002035fd44bf837bdc13c6e5607db472b528a804d248490700000000000000000000cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846d88b656401dd7830351700000000

first_64_bytes = block_header[:64]
print(first_64_bytes.hex())

# The midstate is not a completed sha256 instead it is a reult of a single "update"
# expected midstate: [91 DF EA 52 8A 9F 73 68 3D 0D 49 5D D6 DD 74 15 E1 CA 21 CB 41 17 59 E3 E0 5D 7D 5F F2 85 31 4D]




