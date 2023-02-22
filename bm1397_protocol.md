## BM1397 protocol
notes from sniffing BM1397 communication and trying to match it up with cgminer

It looks like this setting up the BM1397:
```
TX: 55 AA 52 05 00 00 0A // also "chippy"??

RX: AA 55 13 97 18 00 00 00 06 //response from the BM1397

TX: 55 AA 53 05 00 00 03 //chain inactive
TX: 55 AA 53 05 00 00 03 //chain inactive
TX: 55 AA 53 05 00 00 03 //chain inactive
TX: 55 AA 40 05 00 00 1C //"chippy"??
TX: 55 AA 51 09 00 80 00 00 00 00 1C //init1
TX: 55 AA 51 09 00 84 00 00 00 00 11 //init2
TX: 55 AA 51 09 00 20 00 00 00 01 02 //init3
TX: 55 AA 51 09 00 3C 80 00 80 74 10 //init4
TX: 55 AA 51 09 00 14 00 00 00 00 1C //set_ticket
TX: 55 AA 51 09 00 68 C0 70 01 11 00 //init5
TX: 55 AA 51 09 00 68 C0 70 01 11 00 //init5 (second one)
TX: 55 AA 51 09 00 28 06 00 00 0F 18 //init6
TX: 55 AA 51 09 00 18 00 00 7A 31 15 //baudrate
TX: 55 AA 51 09 00 70 0F 0F 0F 00 19 //prefreq
TX: 55 AA 51 09 00 70 0F 0F 0F 00 19 //prefreq
TX: 55 AA 51 09 00 08 40 A0 02 25 16 //freqbuf
TX: 55 AA 51 09 00 08 40 A0 02 25 16 //freqbuf
```

Sending work to the BM1397 looks like this;
```
TX: 55 AA 21 36 38 01 00 00 00 00 20 27 07 17 8A C0 DC 63 F3 CF F8 BF FF 89 BD 99 97 74 10 44 FF 84 D0 74 30 85 44 35 E3 DE 76 0B CC 28 54 A2 FA 12 42 98 00 00 00 00 00 00 00 00 92 C7 00 00 FB 7A BB 8C 33 75 29 7C D8 F6 7E 14 1B D3 0F 2F D1 A0 82 66 00 00 00 20 7F D3
```

How does this big work field break down?

`55 AA` - (most) All of the commands to the BM1397 start with this

`21` - always 21. like bitcoin, always 21 million coins?

`36` - Length

`38` - JobID

`01` - Midstates

`00 00 00 00` - always zero. Is this the starting nonce?

`20 27 07 17` - nbits

`8A C0 DC 63` - ntime → Fri Feb 03 2023 08:06:34 GMT

`F3 CF F8 BF` - last 4 bytes of the merkle root

`FF 89 BD 99 97 74 10 44 FF 84 D0 74 30 85 44 35 E3 DE 76 0B CC 28 54 A2 FA 12 42 98 00 00 00 00` - midstate computed from the first 64 bytes of the header

`00 00 00 00 92 C7 00 00 FB 7A BB 8C 33 75 29 7C D8 F6 7E 14 1B D3 0F 2F D1 A0 82 66 00 00 00 20` - uhoh, what's this?

`7F D3` - checksum

- for the checksum use crc16_false() on all bytes except for `55 AA`


when the BM1397 finds a nonce that makes a hash below the difficulty (which difficulty is that?) it responds with;
```
AA 55 00 03 EA 0F 02 20 9C
```
this means the Nonce `0003ea0f` was found for JobID `20`

If the BM1397 _doesn't_ find a good nonce in the whole nonce space, it just goes off into lalaland. It's up to the miner to keep track of what the ASIC is doing and send it more work if this happens. If you look at the hashing frequency you should be able to time this.
