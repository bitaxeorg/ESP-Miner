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
55 AA 21 96 04 04 00 00 00 00 A3 89 06 17 2F A8 0A 64 6F 1C C5 E8 99 CE 15 A2 DE DF 61 49 77 90 69 84 1C 8A 0C E0 A0 30 E4 0A CB A6 A5 FB 33 19 30 5D E2 46 EF 18 1D 63 63 EB E7 BA D9 8D 4B CA FE 9C 4F 45 F6 45 FA 71 A0 1E 8C B8 2D 68 DC 6C B8 4E 25 39 8C 50 FA 7E 2E C6 C8 08 61 B9 A5 89 90 71 C4 75 56 E4 78 85 35 22 65 51 EA 68 EB F8 96 B0 CA 40 77 D4 0C AF 1B D4 47 37 85 BB 39 6A 22 C3 9C 23 56 E7 CE B6 57 4C 1F A3 A9 9A D3 C1 A0 17 79 1F BC 38 8C 24
```

How does this big work field break down?

`55 AA` - (most) All of the commands to the BM1397 start with this

`21` - always 21. like bitcoin, always 21 million coins?

`96` - Length

`04` - JobID

`04` - Midstates

`00 00 00 00` - always zero. Is this the starting nonce?

`A3 89 06 17` - nbits

`2F A8 0A 64` - ntime → Fri Mar 10 2023 03:46:55 GMT

`6F 1C C5 E8` - last 4 bytes of the merkle root

`99 CE 15 A2 DE DF 61 49 77 90 69 84 1C 8A 0C E0 A0 30 E4 0A CB A6 A5 FB 33 19 30 5D E2 46 EF 18` - midstate computed from the first 64 bytes of the header

`1D 63 63 EB E7 BA D9 8D 4B CA FE 9C 4F 45 F6 45 FA 71 A0 1E 8C B8 2D 68 DC 6C B8 4E 25 39 8C 50` - midstate 2

`FA 7E 2E C6 C8 08 61 B9 A5 89 90 71 C4 75 56 E4 78 85 35 22 65 51 EA 68 EB F8 96 B0 CA 40 77 D4` - midstate 3

`0C AF 1B D4 47 37 85 BB 39 6A 22 C3 9C 23 56 E7 CE B6 57 4C 1F A3 A9 9A D3 C1 A0 17 79 1F BC 38` - midstate 4

`8C 24` - checksum

- for the checksum use crc16_false() on all bytes except for `55 AA`


when the BM1397 finds a nonce that makes a hash below the difficulty (which difficulty is that?) it responds with;
```
AA 55 00 03 EA 0F 02 20 9C
```
this means the Nonce `0003EA0F` was found for JobID `20`

If the BM1397 _doesn't_ find a good nonce in the whole nonce space, it just goes off into lalaland. It's up to the miner to keep track of what the ASIC is doing and send it more work if this happens. If you look at the hashing frequency you should be able to time this.
