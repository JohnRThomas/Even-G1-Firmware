# Packet buffers of the g1 suite

Each `NAME.hex` holds the packets of one bulk transfer of the G1 app, one packet a line as hex bytes, complete with
length and sequence number; `#` starts a comment. A test takes them with `buf('NAME', 3)` (packet 3, from 1) or
`buf('NAME', 2, 24)` (packets 2 to 24), see `Buffers` in `../../engine/statements.py`.

`gen_packets.py` writes all six (`python3 tests/reg/g1/packets/gen_packets.py`), and `tests/reg/check_packets.py`
checks that the files here are what it writes. To change a buffer, change the generator and run it.

The content is made up. Where the app would send a picture, a chart or a map, the buffers hold noise: the first
bytes of SHAKE-128 over a fixed name (`hashlib.shake_128(name.encode()).digest(n)`), so they are the same bytes on
every machine. Only the framing is the app's. Nothing here was captured from a phone.

| buffer | packets | test | what it is |
|---|---|---|---|
| `bitmap` | 51 | `image/bitmap.ref` | the bitmap upload, command `0x15` |
| `stock_gme` | 11 | `dashboard/stocks.ref` | dashboard stock 1 with its chart, command `0x06` sub 4 |
| `stock_aapl` | 11 | `dashboard/stocks.ref` | dashboard stock 2 with its chart, command `0x06` sub 4 |
| `canvas_map` | 36 | `dashboard/canvas.ref` | the map behind the dashboard canvas, command `0x06` sub 7 |
| `nav_overview_map` | 3 | `navigation/session.ref` | the navigation overview map, command `0x0A` sub 2 |
| `nav_panoramic` | 90 | `navigation/session.ref` | the navigation panoramic map, command `0x0A` sub 3 |

## Framing

`bitmap`: `15`, the packet index from 0 (one byte), then 194 bytes of the file; the first packet has the flash
address `00 1C 00 00` between index and data. The last packet has the remaining 156 bytes.

The others: `cmd`, u16 length of the whole packet, seq, `sub`, u16 chunk count, u16 chunk index from 1, the piece
(u16 little-endian; seq goes up by one a chunk; `nav_panoramic` has a `00` between the index and the piece). A
piece is 180 bytes, in the two navigation buffers 185.

## Content

- `bitmap`: a BMP file of 9856 bytes: 576x136, 1 bit a pixel, palette black and white, the 9792 pixel bytes noise
  (name `bitmap`), then two zero bytes that bring the file to a multiple of 4. Shown, it is a full panel of random
  dots. The `16 48 DD 59 D3` that `image/bitmap.ref` sends afterwards is the CRC-32 of the flash address and the file,
  high byte first.
- `stock_gme`, `stock_aapl`: `01 01 02 n 01`, the texts as `{number 1..5, length, text}` (symbol, name, price,
  change, change in percent), `06 01 status`, `07`, u16 1886, then a chart of 1886 bytes of noise (name `chart1`,
  `chart2`). Stock 1 is GME.XNYS, GameStop Corp, 21.36, -0.09, -0.42%, status 2; stock 2 is AAPL.XNAS, Apple Inc,
  270.10, +1.25, +0.46%, status 1. Seq 0x03.. and 0x14...
- `canvas_map`: `01 04 02 01`, then 6392 bytes of noise (name `map`). Seq 0x04...
- `nav_overview_map`: 544 bytes, 272 pairs of `11` and a noise byte (name `overview`). The firmware reads the pairs
  as run lengths (`rle_decode`, `src/utils/common_utils.c`): 17 times each byte, 4624 bytes, the size of its
  overview bitmap and mask (`nav_map_bitmaps` in `src/ui/ui_navigation_map.c`, 2 x 0x908). Seq 0x78...
- `nav_panoramic`: 16592 bytes of noise (name `panoramic`), the size of the 488x136 1-bit map and its mask that the
  firmware draws (2 x 8296 bytes). Seq 0x81...

## Where they come from

The first regression runner built these packets in its scenarios (commit f9788035, `tests/reg/run.py`: the functions
`f`, `chunked`, `noise`, `stock`, `bmp`), and when the scenarios became `.ref` files they were written out as these
buffers. `gen_packets.py` is that code again and gives the same packets byte for byte.

Why these sizes (1886, 6392, 16592 bytes, 272 pairs) is not recorded. A captured session of the app
(`tests/unit/vectors/README.md`, `navigation.hex`) sends the panoramic map in 90 chunks as well, and the overview
map in 9.
