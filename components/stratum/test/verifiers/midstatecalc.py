import hashlib
import binascii


def swap_endian_words(hex_words):
    '''Swaps the endianness of a hexadecimal string of words and returns as another hexadecimal string.'''

    message = binascii.unhexlify(hex_words)
    if len(message) % 4 != 0:
        raise ValueError('Must be 4-byte word aligned')
    swapped_bytes = [message[4 * i: 4 * i + 4][::-1] for i in range(0, len(message) // 4)]
    return ''.join([binascii.hexlify(word).decode('utf-8') for word in swapped_bytes])


version = b'\x04\x00\x00\x20'  # little-endian encoding of version 1
prev_block_hash = bytes.fromhex(swap_endian_words("ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000"))
merkle_root = bytes.fromhex('adbcbc21e20388422198a55957aedfa0e61be0b8f2b87d7c08510bb9f099a893')
timestamp = b'\x64\x49\x55\x22'  # little-endian encoding of Unix timestamp 1683385928
difficulty_target = b'\x39\xc7\x05\x17' # little-endian encoding of difficulty
nonce = b'\x00\x00\x00\x00'  # example nonce value

block_header = version + prev_block_hash + merkle_root + timestamp + difficulty_target + nonce
print(block_header.hex())
# output: 04000020ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000adbcbc21e20388422198a55957aedfa0e61be0b8f2b87d7c08510bb9f099a8936449552239c7051700000000

first_64_bytes = block_header[:64]
print(first_64_bytes.hex())
print(len(first_64_bytes))
block_hash = hashlib.sha256(first_64_bytes).digest()
print(block_hash.hex())
# output: ce9aa3d9ff4cf2c5897fe7caaacf56bfd7fc7ac01d89f8d88f6501d4597344b9

