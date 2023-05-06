import hashlib

def double_sha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def merkle_root(coinbase_tx_hash, merkle_branches):
    current_hash = bytes.fromhex(coinbase_tx_hash)
    for branch in merkle_branches:
        branch_hash = bytes.fromhex(branch)
        # Concatenate the hashes and compute the double SHA-256 hash
        current_hash = double_sha256(current_hash + branch_hash)
    return current_hash.hex()
    
coinbase_tx = '01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008e969579199999999072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000'
coinbase_tx_hash = double_sha256(bytes.fromhex(coinbase_tx)).hex()
merkle_branches = [
    "ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81",
    "980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21",
    "a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52",
    "7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2",
    "2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e",
    "302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc",
    "318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392",
    "1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9",
    "f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1",
    "3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75",
    "463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758",
    "03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76"
]

print("Merkle root:", merkle_root(coinbase_tx_hash, merkle_branches))
